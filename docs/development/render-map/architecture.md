# Render-map architecture

## Purpose

The render map answers two different questions without conflating them:

1. **What can CSX build or invoke?** The shader dependency manifest answers
   this deterministically from source.
2. **What did Skyrim and CSX render in this executable, scene, frame, and eye?**
   Engine facts and runtime evidence answer this with explicit provenance.

The joined model is:

```text
scene object
  -> geometry
  -> shader property / material
  -> temporal geometry setup
  -> BSRenderPass
  -> Skyrim shader family / technique / permutation
  -> CSX compile unit / pass
  -> bound D3D11 pipeline
  -> Draw*, Dispatch, or command-list execution
```

This is a graph, not a row in the shader manifest. Every arrow may be absent,
ambiguous, or many-to-many.

The stable, capture-local geometry declaration and the temporal setup scope are
different node kinds. A `geometry` node is one observed engine geometry
identity. A `geometry-setup` node is one invocation scope that binds that
geometry and an exact material-state revision into a render pass. Keeping them
separate prevents repeated or particle-generated setup work from being
misreported as thousands of distinct meshes.

## Layer 0: prior-art candidate catalogue

`prior-art-catalog.json` records reusable public reverse engineering and
extension architecture before it is admitted to the target map. Source and
target provenance are deliberately separate:

- the source repository is pinned to a full commit;
- source applicability records its Skyrim product/runtime and the basis for
  that attribution;
- shader claims record their originating shader system, version, and revision;
- the target records the exact Skyrim VR executable, CSX version/source
  revision, and generated shader-manifest hash;
- each claim has a target assessment and zero or more explicit spot checks.

A Special Edition or Anniversary Edition reconstruction can provide an
excellent hypothesis for Skyrim VR, but not a fact. Likewise, shared ancestry
between SSE Shader Tools, Community Shaders, and CSX does not establish current
behaviour. The catalogue retains useful hypotheses without weakening the
version boundary of the engine map.

Prior-art claims are never promoted automatically. A target-specific static or
runtime check must produce new evidence, after which a separately reviewed
engine-map change may cite that target evidence. Mismatches remain recorded;
they are often the most useful indication of executable or fork divergence.

## Layer 1: deterministic shader map

`shader-manifest.generated.json` remains authoritative for sources, include
closure, feature defines, compile units, CSX passes, resources, routes, and
invalidation. The render map refers to it by file SHA-256 and stable
`compile-####` or `pass-####` identifiers.

The render-map tooling must fail rather than silently accepting a shader
reference that is absent from the exact manifest named by a capture.

## Layer 2: version-specific engine map

An engine map contains typed entities and evidence-bearing relationships for one
Skyrim executable build. Typical entities are:

- executable and module;
- C++ class and vtable;
- function, virtual function, hook boundary, and callsite;
- render phase;
- shader family, technique, and permutation domain;
- engine-owned render target or resource role.

It does not contain live pointers. Addresses are represented as module-relative
RVAs or relocation IDs and are always accompanied by evidence. A symbol, PDB,
Ghidra analysis, CommonLib declaration, address-library entry, source inspection,
or repeated runtime observation can support a fact. Confidence belongs to each
relationship, not just to the document as a whole.

An engine map can relate one technique to several CSX compile units and passes.
It can likewise relate one CSX pass to several phases or callsites. Singular
`skyrimHost` fields are therefore intentionally excluded.

## Layer 3: runtime evidence

A capture consists of:

- one `capture-manifest.json` describing provenance and bounds;
- one append-only `events.jsonl` stream;
- optional referenced artefacts such as screenshots or GPU captures;
- explicit dropped-event and truncation accounting.

Every event has a monotonically increasing sequence number and timestamp, plus
process, thread, frame, eye, and D3D11 context identity where known. Runtime
objects receive capture-local observation IDs. Raw addresses may be retained as
pointer evidence, but are never used as cross-capture IDs.

### Required correlation discipline

Engine hooks maintain a thread-local context stack. A context token is created
at an owning boundary such as render-pass submission and propagated through
nested technique and geometry calls. A D3D11 event records:

- the active thread-local tokens;
- the specific device-context observation ID;
- pipeline-object observation IDs;
- the current frame and eye epoch;
- parent and causal event sequences where known.

The collector must not associate a draw with a process-global “most recent”
pass. Calls without a provable association are preserved as uncorrelated events.
Uncertainty is data, not a reason to invent an edge.

### D3D11 object identity

COM pointer values may be reused. Each first-seen or creation observation is
assigned a generation-scoped ID. Shader creation records should include a
SHA-256 of supplied bytecode where available. Binding and draw events refer to
the observation ID, with the pointer retained only as supporting evidence.

Deferred device contexts and command lists require distinct context IDs and an
`execute-command-list` event that relates recorded work to the immediate-context
submission epoch.

### Implemented shader identity slice

`BSShader_BeginTechnique` registers the engine shader as a typed, capture-local
`shader` observation before opening the technique scope. The identity key is the
bounded tuple `(pointer, shaderType, fxpFilename, imageSpaceName, compileSourceName,
definesSuffix)`. `compileSourceName` is the exact input CSX uses to construct the
HLSL path: `originalShaderName` for ImageSpace and `fxpFilename` otherwise.
Repeated observations of the same tuple reuse one ID. A changed tuple at the same
pointer, or an explicit retirement followed by reuse, creates the next pointer
generation. Capture generation remains part of every serialized observation ID.

The first sighting emits `shader-observed` with `shader-observation-v2`; technique
events use `technique-boundary-v2` and carry the same typed shader observation ID.
The registry is separately bounded by `maxShaderObservations`. Exhausting that
bound is reported as structural incompleteness rather than silently merging an
unknown shader.

This slice does not claim to prove destruction and recreation when a pointer
returns with an identical tuple. A destructor or creation hook may call the
provided retirement boundary later.

### Implemented technique-resolution slice

`BSShader_BeginTechnique` now brackets the engine call with a thread-local
selection context. The internal vertex/pixel binding call sites record the
object actually passed to D3D, rather than inferring it from Skyrim's current
wrapper globals (which intentionally retain the engine wrapper on a CSX cache
hit). The explicit CSX fallback binding path records its selections too.

Each completed attempt emits `technique-resolved` with input and modified
descriptors, success/skip state, and a route for each stage: `engine`,
`csx-cache`, `csx-fallback`, `skipped`, `missing`, or `unknown`. Selected D3D
objects are capture-local typed `vertex-shader` or `pixel-shader` observations.
Their first sighting emits `stage-shader-observed` v3 with wrapper and D3D pointer
evidence, pointer generation, wrapper descriptor, bytecode size and SHA-256
when the D3D creation hook observed it, and the resolved cache path only when a
CSX cache route actually supplied the object.

At `BSShader::LoadShaders`, process-lifetime provenance also joins each loaded
VS/PS D3D object to its exact loader-family string, effective compile-source name,
and descriptor. This distinction is required for ImageSpace: a specific runtime
alias such as `ISHDRDownSample4` can compile the shared `ISHDR.hlsl` family. A stage
observation retains up to eight aliases plus the exact total; overflow or a
truncated family name is explicit. The mapping remains one-to-many because
different Skyrim descriptors may legitimately share one D3D object or one
bytecode hash.

The stage registry has its own `maxStageShaderObservations` bound. Its overflow
is explicit structural incompleteness and cannot silently merge an unknown
object. SHA-256 is calculated once at D3D shader creation; the render-thread
observation path only copies the retained digest. Vertex, pixel, and compute
shader creation hooks retain the lightweight size/digest identity regardless of
the `Dump Shaders` setting; full bytecode remains opt-in and is retained only
for the existing dump workflow. Bound targets and explicit COM destruction
remain later observation layers.

### Implemented immediate-context execution slice

The immediate D3D11 context now tracks the actual object supplied to
`VSSetShader`, `PSSetShader`, and `CSSetShader`. Draw and dispatch detours emit
`draw-call-v3` and `dispatch-call-v1` only during an explicitly armed capture.
Each execution event joins the effective bound stage objects to the typed stage
catalogue. If an object has no richer engine-wrapper observation, the collector
creates a stage observation from process-lifetime creation provenance when
available, otherwise an explicitly minimal pointer-based observation. Capture
start clears capture-local observation IDs but retains the actual bound D3D
objects. The first captured draw or dispatch lazily declares those inherited
objects, so starting between a stage bind and its execution no longer loses the
VS/PS/CS join. It reuses shader-load aliases where they were actually observed;
it does not invent wrapper, descriptor, cache-path, or
other pipeline state that creation-time evidence cannot provide.

Skyrim's shader `SetupGeometry` call returns before the corresponding D3D11
draw, so its RAII scope cannot honestly enclose execution. A selected setup now
publishes one prepared-geometry observation for the next same-thread draw on
the same capture generation and immediate context. The draw consumes that
identity exactly once; any later selected or unselected geometry setup replaces
or clears it. `draw-call-v3.preparedGeometrySetupObservationId` therefore
expresses a bounded handoff, not an active call-stack scope.

The first observed context call in a capture emits a
`device-context-observation-v1` declaration with capture-local identity, pointer
evidence, pointer generation, immediate/deferred kind, and creation evidence.
Draw and dispatch envelopes use that typed `deviceContextObservationId`. Their
non-null `commandStreamSequence` is monotonic within the observed immediate
context and advances across VS/PS/CS binds as well as draw/dispatch calls. It is
an observed CPU-call order, not a GPU completion timestamp or proof that
unhooked D3D11 state calls did not occur.

### Implemented output-merger target slice

The immediate-context observer detours `OMSetRenderTargets` and
`OMSetRenderTargetsAndUnorderedAccessViews`. Each non-keep call advances the
context-local command sequence and declares first-seen render-target and
depth-stencil view objects as capture-local, generation-scoped identities. An
exact binding-set identity preserves the render-target count, slot positions
(including null slots), and depth target. Repeated calls to the same set reuse
the binding identity but remain separate ordered `render-target-bind` events.
Draw events use `draw-call-v3` and join the last successfully catalogued binding
set. `D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL` advances observed command
order without changing or re-declaring target state.

Both target-view and binding-set catalogues have explicit bounds. Overflow is
reported as structural incompleteness and an uncatalogued binding is never
silently joined to an existing identity. Capture start clears the capture-local
binding identity. If no non-keep OM bind is subsequently observed, the first
immediate-context draw claims a one-shot seed and calls `OMGetRenderTargets` on
that render thread. The queried effective state is emitted as
`render-target-binding-v2` with source `capture-state-snapshot`; a real bind is
source `observed-call`. This is an ordered CPU state query, not a claim that the
original bind occurred inside the capture. Pointer evidence plus a capture-local
pointer generation is not yet equivalent to a creation/destruction proof. No
render-target role or VR eye is inferred from slot number, pointer reuse, call
order, or dimensions.

This slice covers the immediate context only. It does not interpret a deferred
context, command list, or command-list replay as immediate execution. Those
capabilities remain false in the service registry until context and command-list
identity can preserve recording order separately from execution order. Render
target resource descriptions, UAV identities, and the remainder of the pipeline
state are not yet part of the draw identity. The context pointer remains
retained evidence alongside—not in place of—the typed identity.

### Implemented resource-flow graph slice

The immediate-context observer now assigns capture-local identities to D3D11
buffers and textures reached through RTV, DSV, SRV, UAV, copy, and resolve
operations. Resource declarations retain the resource dimension and bounded
descriptor fields. View declarations join a concrete view object to its
resource and retain its format, D3D11 view dimension, subresource coordinates,
and flags. Different views of the same resource therefore converge on one
resource node instead of appearing as unrelated pointer-shaped targets.

Ordered `resource-view-bind` events cover SRVs for every graphics and compute
stage and UAVs for compute and the output merger. Slot-null calls are retained
as state changes. `resource-flow` events cover `CopyResource`,
`CopySubresourceRegion`, and `ResolveSubresource`. Ordinary `Draw` and
`DrawIndexed` are observed alongside the instanced, auto, and indirect calls;
this closes a coverage gap in the earlier execution slice.

Render-event 1.13 separates requested setter arguments from effective queried
state. Each hooked SRV setter is followed by the corresponding stage getter for
the affected slots. Output-merger and compute-UAV setters additionally query
the effective UAV range and all six SRV stages because D3D11 can clear a
conflicting input in a different stage. Changed effective slots are emitted as
`resource-view-binding-v2`; `resource-view-state-observed-v1` records every
queried range, including zero-change results. The first draw performs a
generation-scoped full snapshot only if a prior full post-call scan has not
already seeded the capture.

`tools/build-render-graph.py` deterministically joins a completed capture into
execution and resource nodes. An SRV resource points to the draw or dispatch
that reads it; a draw, dispatch, RTV/DSV/UAV output, copy, or resolve points to
the resource it writes. Every edge cites the declaration and execution event
sequences that establish it. Missing declarations, unknown pre-capture output
state, incomplete captures, and unsupported routes become explicit graph gaps.
The builder does not invent pass names, eye identity, GPU completion, or
deferred-context execution.

When a draw also carries an exact v2 geometry/material scope, the builder
matches the material's capture-local resource identities against the ordered
SRV state effective at that draw. Exact matches are retained on the
resource-version `reads` edge with the material binding identity, stage, slot,
view and evidence sequences. The material-list index remains an engine list
position, not a shader register. Sampler state and proof that shader control
flow reads the bound slot remain separate evidence gaps.

The original binding stream recorded application setter intent. D3D11 may
automatically null an overlapping input/output binding. Graph 1.9 resolved
complete view descriptors into exact D3D11 mip/array subresource spans and
applied the documented overlap rule; unknown descriptor families remained
counted conservative fallbacks. Graph 1.10 retains that deterministic
prediction but uses post-call getter results as the effective draw/dispatch
state. Prediction/query disagreements are explicit counters rather than being
silently resolved in favour of either source.

The capture-start output-merger seed has been qualified in a zero-loss loaded
scene capture. One queried `capture-state-snapshot` preceded the first draw on
the immediate context; normal `observed-call` binds then superseded it. The
derived graph eliminated all 301 previously reported pre-capture target-state
gaps. Re-derivation with graph 1.9 retained all 719 predicted hazard
adjustments with zero range fallbacks, showing that every conflict in this
capture overlaps an exact subresource. This validates effective-state seeding
and exact overlap classification while keeping the remaining post-call state
boundary explicit.

Immediate-context CPU access is represented by `resource-cpu-access-v1`.
Successful `READ` and `READ_WRITE` Map return establishes a CPU read of the
current resource version. A matched writable Unmap publishes a new resource
version. Failed Maps and unmatched Unmaps remain explicit observations but do
not synthesize resource edges. Map/Unmap pairs are keyed by capture generation,
context, allocation and subresource; no pair crosses a capture boundary.
Measured Map-call time is retained as QPC ticks, while mapped lifetime is kept
separate so neither is mislabelled as pure GPU execution time.

## Layer 4: derived render graph

The render graph is regenerated from exact input hashes. It contains normalized
nodes, evidence-bearing edges, unresolved alternatives, capture gaps, and timing
windows. It does not overwrite its inputs or promote inferred relationships
back into an engine map automatically.

Promotion of a runtime correlation into a static engine fact requires explicit
review and new static evidence or repeatable validation across the declared
builds and scenarios.

## Evidence and confidence

Every relationship uses one of these evidence classes:

| Class | Meaning |
|---|---|
| `source-proven` | Directly established by tracked source or generated manifest |
| `static-reverse-engineered` | Established by binary, symbols, disassembly, or type information |
| `runtime-observed` | Directly emitted by an instrumented boundary |
| `correlated` | Joined from compatible identities, scopes, and event ordering |
| `validated-across-runs` | Reproduced under the declared build and scenario matrix |
| `inferred` | Plausible but not established; alternatives must remain visible |

Confidence is `confirmed`, `high`, `medium`, `low`, or `unknown`. A graph
generator may reduce confidence when joining facts; it must never increase
confidence merely because several inferred edges form a complete-looking path.

## Timing model

Frame number alone is insufficient. The event stream distinguishes:

- scene accumulation epoch;
- depth-source production and completion;
- visibility candidate generation;
- visibility-result readiness;
- pass construction and sorting;
- eye submission;
- draw/dispatch submission;
- command-list execution;
- result consumption in a later frame.

QPC timestamps order CPU observations; they do not prove GPU execution or
completion. Visibility readiness is therefore explicit as `cpu-observed`,
`gpu-ordered`, `gpu-completed`, `gpu-resource-consumable`,
`cpu-readback-complete`, or `unknown`. A decision-window comparison is valid
only when its evidence domains establish readiness for the proposed consumer.
GPU-only consumers may rely on proven command-stream ordering and resource
hazards without CPU readback. CPU consumers require completion/readback evidence
and must account for any synchronization stall.

The derived graph records a **decision window** for every culling candidate:
the earliest point at which the required visibility fact is valid, the last
point at which a selected suppression mechanism can act, and whether the window
is viable. This makes current-frame, previous-frame, CPU-side, and GPU-side
culling claims testable.

## Capture lifecycle

1. Validate exact shader manifest, engine map, executable, DLL, profile, cache,
   runtime, and scenario fingerprints.
2. Arm a bounded capture before entering the target frame range.
3. Emit a start marker and reset capture-local identities.
4. Capture only enabled event categories, with a fixed byte/event budget.
5. Emit gaps immediately when records are dropped.
6. Stop on the requested condition or budget boundary.
7. Flush, hash, and close artefacts.
8. Write final counts and completeness to the capture manifest atomically.
9. Validate schemas and references before a capture is eligible for joining.

An interrupted capture remains evidence but is marked incomplete. A consumer
must not treat absence from an incomplete stream as proof that an event did not
occur.

## Instrumentation boundaries

The first implementation should favour boundaries already controlled or
understood by CSX:

1. `BSShader::BeginTechnique` or family-specific equivalent;
2. `BSShader::SetupGeometry` or family-specific equivalent;
3. `BSBatchRenderer::RenderPassImmediately`;
4. bounded immediate-context `ID3D11DeviceContext::Draw*` and `Dispatch`
   observation (implemented), followed by explicit deferred-context and
   command-list coverage;
5. render-target/depth-target changes needed to identify the active phase;
6. depth-culling candidate, result-ready, and consume boundaries.

Pipeline state should be tracked incrementally on state-setting calls and
snapshotted by observation ID at a draw. Querying the full D3D11 state at every
draw is a diagnostic fallback, not the default collector design.

## Performance and safety

- Collection is off by default and bounded by time, frames, events, and bytes.
- Hot hooks write compact fixed-envelope records to per-thread buffers.
- Formatting, hashing large artefacts, and graph joins occur off the render
  thread or after capture.
- Capture records QPC frequency, buffer pressure, dropped events, and collector
  overhead samples.
- A performance conclusion additionally requires a retained frame-pacing
  context: observed FPS/frame-time distribution, configured cap and present
  interval, compositor refresh/runtime route, reprojection or frame-doubling
  mode, limiter state, GPU saturation, capture load, and loading/menu/compiler
  activity. An unexplained rate below both cap and demonstrated GPU capability
  marks timing evidence contaminated without discarding structural identity or
  command-order evidence.
- A capture option may redact pointers while retaining observation IDs.
- Instrumentation changes observation only; culling policy changes are tested
  separately so measurement cannot silently alter the thing being measured.
- A loaded Breezehome frame has now been observed to exceed 20,000 render-map
  events. A complete high-density frame therefore also needs event-class
  selection, an initial D3D-state snapshot, or catalogue identity persistence
  across bounded segments; merely increasing one fixed aggregate budget trades
  event truncation for catalogue truncation.

## Ownership boundary with depth culling

The render-map system owns identity, timing evidence, correlation, and reports.
The depth-culling feature owns candidate generation, visibility policy, and the
chosen suppression mechanism. Depth culling may emit domain events through the
collector, but the collector must not decide whether an object is visible.
