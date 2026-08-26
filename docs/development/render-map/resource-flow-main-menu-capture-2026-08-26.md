# Resource-flow main-menu capture, 2026-08-26

## Result

The resource-flow graph slice passed its bounded live mechanism gate at
Skyrim VR's main menu. The derived artifact is an observed immediate-context
execution/resource graph: draw and copy operations are joined to the typed
D3D11 resources they read and write through ordered SRV, RTV, DSV, and copy
evidence. It does not infer resource names, VR-eye roles, or unobserved pass
semantics.

The deployed DLL was built from the dirty source state based on
`5a5b3b6937188a0ed589f12e9fcee3ab778a87b3`. Its SHA-256 was
`B0D5C6379A99C065986B6A23E5967CA50AC8E25B33AAD2224A554F49E776EE24`.
The runtime build ID was
`63a9fb9b490f53d20af5ed66e50d43d96ff56783505db052e427c6f43143e742`.

## Capture bounds and completion

Capture `capture-live-00001aa5cedc7e0c-1` requested one frame, 1,000 ms,
65,536 events, 32 MiB of collector storage, and the maximum v1 shader,
resource, view, and binding catalogue bounds. It completed on the frame bound
with:

- 228 accepted events, sequences `0` through `227`;
- 20 resource, 20 target-view, and 10 target-binding observations;
- no truncation and no dropped events or observations;
- one expected boundary rejection after the frame limit;
- a complete atomic manifest and `events.jsonl` artifact.

The stream contained 121 ordered resource-view binds, 26 draws, three
copy/resolve flow events, and one observed engine render-pass scope. It also
contained the shader, technique, and geometry observations needed for later
semantic overlays.

## Derived graph

`tools/build-render-graph.py` produced a schema-valid graph containing:

| Node or edge | Count |
|---|---:|
| Draw nodes | 26 |
| Copy/resolve nodes | 3 |
| Resource nodes used by an execution | 17 |
| Confirmed read edges | 86 |
| Confirmed write edges | 54 |
| Gaps | 0 |
| Ambiguities | 0 |

The observed roles include pixel SRVs, a vertex SRV, render targets, depth
stencils, and copy sources and destinations. Every edge cites the resource
declaration and execution event sequences that establish it. The graph does
not create nodes for three declared resources that no captured execution used.

This is a genuine resource/execution graph, but not yet a complete semantic or
versioned frame graph. A resource node currently denotes a D3D11 allocation;
it does not split each write into a distinct resource version. Consequently,
the graph is suitable for observed usage, dependency investigation, and
coverage measurement, but should not yet be treated as a frame-graph DAG.

## Retained evidence

Authoritative evidence is retained at
`L:\Codex\evidence\render-map\20260826-main-menu-resource-flow`.

| Artifact | SHA-256 |
|---|---|
| `events.jsonl` | `E1C0A37FB2DDF000602E03E3109B64A123AE281FB60EF31CB66BA40C963AD377` |
| `capture-manifest.json` | `546F2267088559E385C8A462AEE4E189AF39E83B636A165F6C29191BDA1E23B0` |
| `render-graph.json` | `0EF828E32AF4D30F0423F18AE2F2F5783F21CC101BC1C8106ACD905911D873F3` |

The main-menu capture is intentionally a mechanism test. It cannot substitute
for an in-game world-scene capture when classifying G-buffer, shadow, water,
weather, or feature-owned resources.

## Next boundary

The next graph slice should add resource versions and observed execution-order
hazards, then project engine render-pass, technique, geometry, and bound-shader
scopes over the lossless execution graph. Deferred contexts, command-list
replay, D3D11 automatic hazard unbinding, and VR-eye attribution remain
explicitly unsupported until separately observed.
