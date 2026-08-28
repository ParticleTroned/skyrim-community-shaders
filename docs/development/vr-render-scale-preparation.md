# VR render-scale preparation telemetry

The direct CS-menu render-scale preparation path remains synchronous. An audit
of every operation reachable from
`Upscaling::PreparePendingVRRenderScaleTransition` found no unit of work that
is both CPU-only and independently publishable under the current ownership
model. Moving any current stage to a worker would therefore move a D3D or live
provider operation, or would require a new shader-cache authority.

Preparation remains optional. Failure, cancellation, supersession, or a device
change leaves the protected relatch and its existing presentation fallback in
control. A prepared result can shorten only the ordinary direct-menu delay; it
does not bypass device-loss, physical-mutation, resource-identity, provider, or
stereo-presentation safety checks.

## Operation classification

| Operation                       | CPU bytecode                                       | D3D11 object                                        | D3D12/provider                                                           | Mutable global state                                                                        | Thread/context/locks                                                                     | Cancellation                      | Decision                                  |
| ------------------------------- | -------------------------------------------------- | --------------------------------------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | --------------------------------- | ----------------------------------------- |
| Admission and early-exit checks | No                                                 | No                                                  | No                                                                       | Reads controller, device, reset and shader-cache state                                      | Render/configure path; controller lock only for the bounded memory snapshot              | Safely discardable                | Keep synchronous; it is the gate          |
| Shader-cache busy deferral      | No                                                 | No                                                  | No                                                                       | Reads the global compilation state                                                          | Render/configure path; no device-context call                                            | Retry on a later frame            | Keep synchronous; never wait for a future |
| SSS raymarch prewarm            | `D3DCompileFromFile` on a miss                     | `CreateComputeShader` on a miss                     | No                                                                       | Mutates the bounded raymarch variant cache and `LazyShader` failure latch                   | Render/configure path; live D3D11 device; no immediate-context call or cancellation lock | None within a compile             | Keep synchronous                          |
| SSGI prewarm                    | `D3DCompileFromFile` for each required permutation | `CreateComputeShader` for each compiled permutation | No                                                                       | Reads mutable SSGI settings and replaces the feature shader set only after complete success | Render/configure path; live D3D11 device; no immediate-context call or cancellation lock | None within a compile             | Keep synchronous                          |
| DLSS preparation                | No                                                 | No                                                  | Reads live Streamline feature, viewport, input-resource and option state | Reads Streamline session/resource ownership                                                 | Supported render path; no immediate-context call                                         | No independent artefact to cancel | Keep synchronous                          |
| FSR host preparation            | No                                                 | No                                                  | Reads live FidelityFX host resources                                     | Reads current host resource ownership                                                       | Supported render path; no immediate-context call                                         | No independent artefact to cancel | Keep synchronous                          |
| Runtime FSR/FSR4 preparation    | No                                                 | No                                                  | Reads live D3D11/D3D12 interop and runtime-provider compatibility        | Reads current runtime context, failure latches and version selection                        | Supported render path; provider state is thread-affine                                   | No independent artefact to cancel | Keep synchronous                          |

`Util::CompileShader` is deliberately not classified as CPU-only. It reads the
global define snapshot and shader-cache mode, compiles bytecode, creates the
typed D3D11 shader on the current device, and names that object. The optional
timing accumulator separates the compile and D3D creation intervals without
changing this ownership.

## Exact preparation proof

Each accepted request receives a nonzero immutable preparation-options
generation. Completion is published only when all of these still match the
candidate captured before work began:

-   request ID and transition epoch;
-   preparation-options and shader-defines generations;
-   D3D11 device identity;
-   method, quality mode, DLSS preset and FSR4 selection;
-   render-scale enable state;
-   render and display eye dimensions.

A newer request, changed options, changed shader defines, or changed device
discards completion. Device-dependent prepared state is also rejected at every
prepared fast-path read. Preparation remains one-shot for the exact request ID;
an invalidated result retains the protected relatch instead of rerunning work
under that request.

## DevBench schema

`communityshaders.renderscale` status contains
`status.preparation` with schema version 1. Collection is active only while the
bounded render-scale stress session is active. The ring retains 512 records,
reports overwritten records, and coalesces identical repeated early-exit or
busy observations. Repeated checks retain their latest call duration; the
`shader_cache_busy_wait` duration spans the first through last busy
observation.

Every event contains:

-   session ID, request ID, transition epoch, preparation-options generation,
    shader-defines generation and D3D device identity;
-   method, quality, DLSS preset, FSR4 selection, and render/display eye sizes;
-   first/last frame, occurrence count, event, outcome, reason mask and decoded
    reasons;
-   begin/end QPC, QPC duration and milliseconds;
-   bytecode-compilation and D3D-object-creation QPC durations and milliseconds.

Event names are:

-   `request_queued`, `admission_check`, `early_exit`, and
    `shader_cache_busy_wait`;
-   `sss_raymarch_prewarm`, `ssgi_prewarm`, `dlss_preparation`,
    `fsr_preparation`, and `fsr4_preparation`;
-   `d3d_object_creation`, `total_preparation`, `request_to_prepared`, and
    `prepared_to_creator`.

`shader_cache_busy_wait` is an observed deferred interval, not a blocking wait.
No worker future is created or waited on. `request_to_prepared` is emitted only
when its exact queued event is retained, and `prepared_to_creator` only when the
creator consumes the same transition epoch.

## Measurement decision

Use the per-stage measurements from controlled in-game transitions before
revisiting asynchronous preparation. A future move is admissible only if a
measured stage can be split into an immutable CPU artefact with existing
thread-safe task infrastructure, bounded cancellation/supersession, and a
render-thread D3D/provider commit. The present implementation intentionally
moves no work off-thread.
