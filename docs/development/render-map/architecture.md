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
  -> BSRenderPass
  -> Skyrim shader family / technique / permutation
  -> CSX compile unit / pass
  -> bound D3D11 pipeline
  -> Draw*, Dispatch, or command-list execution
```

This is a graph, not a row in the shader manifest. Every arrow may be absent,
ambiguous, or many-to-many.

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
4. bounded `ID3D11DeviceContext::Draw*` and `Dispatch` observation;
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
- A capture option may redact pointers while retaining observation IDs.
- Instrumentation changes observation only; culling policy changes are tested
  separately so measurement cannot silently alter the thing being measured.

## Ownership boundary with depth culling

The render-map system owns identity, timing evidence, correlation, and reports.
The depth-culling feature owns candidate generation, visibility policy, and the
chosen suppression mechanism. Depth culling may emit domain events through the
collector, but the collector must not decide whether an object is visible.

