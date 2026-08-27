# Resource-flow render graph slice

## Question answered

This slice answers: **which observed version of a D3D11 resource did an
immediate-context execution read, which new version did it write, and which
ordered RAW/WAR/WAW dependencies follow from those accesses?**

It is deliberately narrower than a full semantic Skyrim frame graph. It does
not label a texture as G-buffer normal, shadow map, eye color, or water
reflection without separate evidence.

## Runtime evidence

- typed buffer and texture declarations;
- typed RTV, DSV, SRV, and UAV declarations joined to their resources;
- ordered SRV state for VS, HS, DS, GS, PS, and CS;
- ordered UAV state for CS and the output merger;
- RTV/DSV binding-set identity at draw;
- `Draw`, `DrawIndexed`, instanced, auto, indirect, and dispatch calls;
- copy-resource, copy-subresource-region, and resolve-subresource operations;
- update-subresource, structure-count copy, RTV/UAV/DSV clear, and mip-generation
  mutations.

All observation registries and the event stream remain explicitly bounded.
Resource registry overflow marks the capture structurally incomplete.

## Derivation

Run:

```powershell
python tools/build-render-graph.py `
  --capture-manifest <capture>/capture-manifest.json `
  --events <capture>/events.jsonl `
  --output <capture>/render-graph.json
```

Optional exact shader-manifest and engine-map paths may be supplied. Their
hashes are retained as graph inputs, but this first resource-flow joiner does
not promote static semantic labels into the runtime graph.

The output is deterministic for the same input files and source commit. It uses
individual immediate-context executions rather than guessed pass boundaries.
Each observed allocation owns an initial capture-entry content version and one
new version for every observed write. A read consumes the most recent version
known at that point in the command stream. The version scope is deliberately
`whole-resource-conservative`; it does not claim that two disjoint views of one
allocation overlap.

The joiner derives execution-order constraints as `precedes` edges:

- **RAW** — a read consumes content produced by a prior write;
- **WAR** — a write must follow readers of the prior content version; and
- **WAW** — a write supersedes a prior write to the same allocation.

These edges are `correlated` with high confidence, not `runtime-observed`: the
underlying calls and bindings are observed, while the dependency label is a
deterministic consequence of the conservative allocation-wide model.

The joiner also reconstructs the minimum D3D11 automatic hazard behaviour
needed to avoid impossible input edges. Binding an allocation as RTV, DSV, or
UAV clears conservatively overlapping SRV slots; requesting an SRV while the
same allocation remains an output is represented as an effective null bind.
The graph reports how many requested bindings were adjusted. This is a
conservative reconstruction, not a query of the state actually returned by the
D3D11 runtime.

The first bounded live validation is recorded in
[`resource-flow-main-menu-capture-2026-08-26.md`](resource-flow-main-menu-capture-2026-08-26.md).
The original `resource-flow-1` joiner produced 46 observed nodes and 140
confirmed read/write edges from one main-menu frame without truncation, gaps,
or ambiguities. Regenerating that retained capture with
`resource-versions-1` is the live gate for this slice and intentionally adds one
non-blocking capability gap for exact subresource/hazard proof.

## Known limits

- immediate context only;
- eye remains unknown until a separately validated eye boundary is joined;
- D3D11 automatic hazard unbinding is conservatively reconstructed for
  same-allocation SRV/output conflicts, but is not emitted as an explicit event
  or verified by `Get*` state queries;
- view subresource fields are normalized evidence, not yet a full overlap
  solver for every D3D11 view dimension;
- maps/unmaps, stream-output, indirect argument-resource reads, and command-list
  replay are not yet resource-flow nodes;
- DSV access is conservatively treated as read/write until format and
  read-only-flag semantics are resolved.

These are reported as capability limits rather than concealed by a
complete-looking diagram.
