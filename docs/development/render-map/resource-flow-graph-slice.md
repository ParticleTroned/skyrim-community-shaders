# Resource-flow render graph slice

## Question answered

This slice answers: **which observed D3D11 resources did an immediate-context
execution read and write, and which copy/resolve operation carried data between
resources?**

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
- copy-resource, copy-subresource-region, and resolve-subresource operations.

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

The output is deterministic for the same input files and source commit. It
uses individual immediate-context executions rather than guessed pass
boundaries. Later tooling may derive pass groups from these nodes without
discarding the underlying execution graph.

The first bounded live validation is recorded in
[`resource-flow-main-menu-capture-2026-08-26.md`](resource-flow-main-menu-capture-2026-08-26.md).
It produced 46 observed nodes and 140 confirmed read/write edges from one
main-menu frame without truncation, gaps, or ambiguities.

## Known limits

- immediate context only;
- eye remains unknown until a separately validated eye boundary is joined;
- D3D11 automatic hazard unbinding is not yet emitted as an explicit event;
- view subresource fields are normalized evidence, not yet a full overlap
  solver for every D3D11 view dimension;
- clears, updates, maps, stream-output, indirect argument-resource reads, and
  command-list replay are not yet resource-flow nodes;
- DSV access is conservatively treated as read/write until format and
  read-only-flag semantics are resolved.

These are reported as capability limits rather than concealed by a
complete-looking diagram.
