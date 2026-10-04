# Render-map runtime

The render-map runtime is a bounded, opt-in diagnostic collector for observing
Community Shaders and Skyrim rendering state. It records render-pass,
technique, geometry, material, shader, D3D11 resource/view/binding, CPU-access,
resource-version, draw/dispatch, culling-observation, and accepted eye-submit
events.

The collector is inert until a controller starts a capture. Capture bounds
limit event count, byte use, string use, and frame duration; truncation and
gaps are represented explicitly. Stopping a capture produces an immutable
completed-capture snapshot which the artifact layer can serialize without
holding render-thread state.

The runtime, its D3D and engine hooks, and its integration call sites are
developer instrumentation. They are compiled only when
`DEVBENCH_BRIDGE=ON`. A normal release build with `DEVBENCH_BRIDGE=OFF`
excludes the `src/RenderMap` translation units and retains the original
non-mapping hook paths.

This runtime does not change depth-culling decisions or apply culling results.
Depth-culling events are observations of the existing upstream behaviour.

Shader creation metadata is retained outside captures so an inherited binding
can have bytecode provenance. A private-data callback owns that metadata until
D3D releases its reference; cache eviction alone does not retire a still-bound
shader. Cleanup holds weak catalogue ownership and is safe after runtime
teardown. Failed callback attachment leaves creation metadata unavailable.
Each creation catalogue admits at most 65,536 live identities. Raw bytecode
is retained only for dump mode, within a 64 MiB shared budget; unavailable dumps
are skipped with a warning instead of producing empty files. Admission failure
does not change the native shader-creation result.

Only lifetime-tracked creation records accept engine aliases. Each retains
eight aliases with bounded names; overflow sets `engineAliasesTruncated`.
After this retention limit, `engineAliasTotalCount` is a lower bound of nine,
not an exact count of every unretained alias. Shaders created before the hooks
were installed cannot acquire lifetime-backed creation provenance later.

The contract runner treats an explicit `-PythonExecutable` as authoritative.
A missing or failing requested interpreter cannot silently use an ambient
fallback. Automatic interpreter discovery applies when the argument is omitted.

## Included here

-   `Collector`: bounded event and string storage with explicit gap accounting.
-   `Runtime`: typed observation entry points used by engine and D3D11 hooks.
-   `Controller`: single-session start, status, stop, and completed-capture
    ownership.
-   `Artifacts` and `Serialization`: deterministic JSONL event and capture
    manifest output.
-   engine, shader, D3D11 context, and OpenVR eye-submit instrumentation.
-   deferred-context recording and exact command-list execution replay, described
    in [`device-context-command-list-slice.md`](./device-context-command-list-slice.md).
-   the controlled deferred-output timing case study and its adjacent structural
    capture requirements, described in
    [`deferred-gbuffer-performance-evidence.md`](./deferred-gbuffer-performance-evidence.md).
-   focused collector, runtime, controller, and offline graph tests.
-   runtime capture-manifest, render-event, and derived render-graph schemas.

Render-event schema revision 1.17 defines fail-closed deferred-command
semantics: command-recording draw/dispatch events require typed recording and
context identities, while missing or contradictory evidence yields explicit
gaps rather than borrowing immediate-context bindings. It also standardizes the
nullable missing-recording `FinishCommandList` form and forbids failed finishes
from naming a materialized command list.

Derived graph producer `static-semantic-resource-graph-10` independently
reconciles immutable device-context, recording, and command-list declarations
across event envelopes and payloads. Contradictory ownership chains now produce
blocking gaps and no authoritative `records`, `materializes`, `finishes`, or
`executes` edge. A restore-false execution also resets all observed and
predicted immediate SRV, UAV, and target-binding state before later work is
derived.

## Deliberately separate

The DevBench registration adapter is included in the same composition as the
runtime. Shader dependency
analysis, generated shader manifests, engine maps, Ghidra helpers, prior-art
catalogues, and captured-analysis reports remain development tools; they do
not enter the Community Shaders binary in either build mode.

## Controller and DevBench composition

The optional `communityshaders.render_map` adapter is included with this runtime
and registered through the existing plugin lifecycle when DevBench is present.
Its actions are `registry`, `status`, `start`, `stop`, and `capture_events`.
Capture is off by default. Start retains runtime provenance and its response
before activating hooks; failed preparation leaves no active capture. Stop
requires the original capture-start provenance, publishes events and a manifest
without overwriting existing evidence, and retains completed captures for paging.
Ordinary builds exclude the Render Map translation units and use inline inert
adapter entry points. Registry payload schemas match current serializer outputs.
The adapter's `deferredContexts` and `commandLists` capabilities remain false
until bounded live coverage is qualified. Prior stacked PRs are independent
historical branches, not a runtime dependency of this composition.

The feature and contract coverage of the earlier runtime and adapter proposals
is recorded in [the supersession audit](./upstream-supersession.md). Historical
build and review results keep their original source identities; current binary
and live qualification remain independent gates.

Before publishing stop artifacts, the adapter reserves its cache entry and
prunes older retained state. Cache admission failure writes no files. A
verified bundle moves into that entry without allocation before constructing
the response. If response construction fails, a fresh `stop` command for the
retained capture returns the same paths, hashes and byte counts without
rewriting either file. This recovery requires the live process and retained
capture/cache; it does not recover a bundle after process exit. Existing
unrelated files still fail the writer's no-overwrite contract.

When execution is restricted to selected geometry, draw/dispatch selection also
resolves paired geometry boundaries and their declarations. The requested mask
retains the caller's selection. Immediate draws may consume a prepared identity
after setup returns; deferred draws require the active selected geometry scope.

Summary `completion.truncated` reports event or structural evidence loss,
including catalogue and scope faults. Manifest `completion.truncated` reports
lost events specifically, represented by a synthetic gap. Both use the same
reason model, exposed by summary `completion.incompleteReasons` and manifest
extension `csx.captureIncompleteReasons`. Shutdown/failure termination alone
makes evidence incomplete without claiming truncation. Summary `state: complete`
means the capture finished; `completion.incomplete` reports evidence loss or
failure. Frame/time bounds and intentional filtering remain separate counters.

## Persistent shader diagnostic storage

Bridge-enabled shader provenance is retained independently of a live capture.
Registry/status `shaderMetadata` declare separate limits: 65,536 bytecode
identities, 65,536 stage identities, eight bounded aliases per stage identity,
and 64 MiB of retained optional dump bytes. These limits are not included in
the live capture's `maxBytes`; neither is a measured process-memory limit.
Creation metadata retires through an attached D3D private-data reference whose
cleanup retains only weak catalogue ownership. Failed attachment withholds
metadata. Retirement reclaims identity/dump capacity; unavailable dumps never
reuse an older byte sequence. Engine aliases require admitted creation metadata.

Vertex, pixel and compute creation observations run behind a catch-all boundary
that returns the original native HRESULT and output without modification. Missing
outputs and failed native calls skip diagnostics. Hash/storage/registration/map
exceptions and admission or cleanup failures increment the allocation-free
`failureCount`; no logging or allocation is performed by the exception handler.
COM teardown and native lifetime behavior still require live qualification.
