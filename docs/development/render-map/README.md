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

The DevBench registration adapter is reviewed separately because it is the
optional external control surface over this runtime. Shader dependency
analysis, generated shader manifests, engine maps, Ghidra helpers, prior-art
catalogues, and captured-analysis reports remain development tools; they do
not enter the Community Shaders binary in either build mode.
