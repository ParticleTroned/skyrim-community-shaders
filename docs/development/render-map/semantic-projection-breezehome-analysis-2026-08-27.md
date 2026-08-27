# Semantic render-graph projection — Breezehome retained capture — 2026-08-27

## Result

The `semantic-resource-graph-1` joiner was run against the retained, bounded
two-frame Breezehome capture `capture-live-0000314145be5778-2`. The resulting
graph validates against the derived-render-graph schema and remains acyclic.

This is an offline re-derivation of existing evidence. It does not claim that a
new runtime capture was performed.

## Proven coverage

The graph contains 5,865 draw nodes:

- all 5,865 bind at least one exact capture-local stage-shader observation;
- 5,814 carry an exact active render-pass scope;
- 51 occur before or outside an observed render-pass scope and remain honest
  unscoped draws;
- 19 engine-shader, 172 technique, 560 geometry, 133 stage-shader/pipeline,
  and 3,397 render-pass identities are projected into the same graph as the
  versioned resource flow;
- 70,179 evidence-bearing edges are emitted, including 11,342 stage-shader
  binds, 5,814 render-pass-to-draw edges, 658 selections, and 560 explicit
  render-pass-to-geometry setup relationships.

Every relationship above is driven by a capture-local scope or observation ID.
Pointer equality is not used to merge identities.

## Boundary exposed by the projection

The projection does not pretend that setup scopes remain active at the D3D11
draw call. In this capture, draws carry render-pass scope but not technique or
geometry scope. Technique resolution and geometry setup are represented as
their own exact observations; joining either to a later draw requires an
explicit persistent submission/material identity or a separately documented
correlation rule.

No node in this retained capture has a shader-manifest or engine-map source
reference. The files are recorded as deterministic graph inputs, but the live
events did not inject their stable IDs. Therefore the graph proves runtime
engine/stage identity and resource flow, but does not yet prove the final
runtime-to-static `compile-####`, `pass-####`, or `engine-*` join.

The existing 4,780 `uncorrelated` gaps remain output-merger state inherited
before capture or otherwise not catalogued; semantic projection does not erase
those capability boundaries. The sole `unsupported-route` gap remains exact
subresource overlap/deferred-context coverage.

## Reproduction and provenance

Joiner commit:
`8cd2cb41582416deec50804a75d5594f87e86f6b`

Retained output:
`L:/Codex/evidence/render-map/20260827-semantic-projection-breezehome/render-graph.json`

| Input/output | SHA-256 |
|---|---|
| capture manifest | `93DAFAB6B9FE1D7C8B61D6F7672F9945AD9C5B53D1974DAE66D33328A77F22C3` |
| events | `0F879CD5358D2CF8B898CE86D843C9AF90205D0B53F869D282A422AC59FE5762` |
| shader manifest | `B78FE890DBC42E865A3D6E659416EF086E39B253C56BBD9C16516268020FA2B2` |
| Skyrim VR engine-map seed | `F9659C84BA418F6CBA1E7E64EC1ABE2AB01BAAB6B139D4F86E623AA6725C1331` |
| derived graph | `CD3304E0F14CB788C8059A0E14C5627B1CC59E8492D0F6A46132C25DBDE97588` |

The derived graph is 62,660,417 bytes and is retained as evidence rather than
committed to Git.

## Next gate

The next live capture should exercise the depth-culling decision window and
accepted-eye route with zero dropped events. Independently, the next semantic
identity slice should add externally verified build/manifest/engine provenance
at capture start and a persistent object/pass-submission identity at the
narrowest engine boundary. That is the shortest route from exact runtime
shader objects to stable static shader-family/pass IDs without proximity or
pointer heuristics.
