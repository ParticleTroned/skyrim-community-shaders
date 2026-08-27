# Resource-version main-menu capture, 2026-08-26

## Result

The `resource-versions-1` slice passed its first bounded live gate at Skyrim
VR's main menu. The capture exercised event schema minor 8 and the resource
mutation hooks, then derived a schema 1.2, acyclic allocation/content-version
graph with correlated RAW, WAR, and WAW dependencies.

This remains a mechanism gate. The main-menu frame is not evidence for
world-scene resource roles, exact view-subresource overlap, deferred contexts,
command-list replay, or VR-eye attribution.

## Runtime identity and controller gate

The task-owned MO2 profile loaded the exact deployed DLL:

| Identity | Value |
|---|---|
| Source commit | `00551cf3af4d84d3c3447c22c72e7f1e23d90576` (dirty) |
| Build ID | `ff9fbf8438d61d9619b0ab5c2ab8d750804c6b3a2393cb12a7e16d53f3602221` |
| DLL SHA-256 | `35506120B3134025BB9EADF5062175AC0A1A6F237E3ED67A516D8613B23DD4B7` |
| DevBench listener | Verified as the responsive `SkyrimVR.exe` process |
| Render-map contract | Major 1, minor 6, schema revision 7 |
| Required capability | `resource-mutation-observations` present |

The in-process manifest correctly leaves the runtime route and unavailable
provenance fields unknown. The deployment harness separately verified the
artifact path, SHA-256, and service-registry build ID.

## Capture bounds and completion

Capture `capture-live-00002e7de1236f80-1` requested one frame, 1,000 ms,
65,536 events, 32 MiB of collector storage, and the maximum v1 shader,
resource, view, and binding catalogue bounds. It completed on the frame limit
with:

- 660 accepted events with contiguous sequences `0` through `659`;
- event schema minor 8 on every event;
- 39 resource, 50 target-view, and 25 target-binding observations;
- 12 shader and 37 stage-shader observations;
- no truncation, dropped events, dropped observations, scope mismatches, or
  scope overflow;
- one expected boundary rejection after the frame limit; and
- complete atomic manifest and event artifacts.

The 65 resource-mutation events used only declared operations:

| Operation | Count |
|---|---:|
| `update-subresource` | 44 |
| `clear-render-target` | 10 |
| `clear-depth-stencil` | 6 |
| `copy-resource` | 3 |
| `copy-subresource-region` | 2 |

All destination-only operations carried a null source-resource identity.

## Derived graph

`tools/build-render-graph.py` produced a schema-valid graph with producer
`resource-versions-1`:

| Node or edge | Count |
|---|---:|
| Nodes | 416 |
| Edges | 990 |
| Allocation nodes | 39 |
| Content-version nodes | 235 |
| Draw nodes | 77 |
| Resource-operation nodes | 60 |
| Copy nodes | 5 |
| Read edges | 287 |
| Write edges | 196 |
| Allocation ownership edges | 235 |
| Dependency edges | 272 |

Every used allocation owns version 0. The graph contains exactly one later
content version for each write edge, and every read edge targets the latest
content version preceding that execution. The dependency edges are all
forward-ordered, correlated evidence:

| Hazard | Count |
|---|---:|
| RAW | 96 |
| WAR | 8 |
| WAW | 168 |

`csx.graphAcyclic` is true. `csx.effectiveStateAdjustments` is zero for this
frame, so the conservative SRV/output suppression path was not naturally
exercised. There are no ambiguities and one non-blocking capability gap: exact
view-subresource overlap and the actual state returned by D3D11 hazard
resolution remain unobserved. That is the sole expected gap for this slice.

## Shader-cache observation

The managed cache packs were unchanged by loading this new DLL build. Only
`Info.ini` changed, replacing the prior build ID with the deployed build ID;
all four `.csxpack` hashes remained identical. This is direct evidence that a
DLL build-ID change alone did not trigger shader recompilation for this run.
The automation preserved that working variant as unverified evidence and
restored the exact pre-run cache tree.

## Retained evidence

Authoritative evidence is retained at
`L:\Codex\evidence\render-map\20260826-resource-version-live-gate`.

| Artifact | SHA-256 |
|---|---|
| `capture-live-00002e7de1236f80-1/events.jsonl` | `70B2C1A58DC70E8E812EF5C96461FBBCBCA0E9C1970D88CAB1EE0876EA47EA4F` |
| `capture-live-00002e7de1236f80-1/capture-manifest.json` | `E5B831992F3CFD63C5734F784596F7E04D9D56F4DAE5EAADB4E7E9EDFC075C1D` |
| `capture-live-00002e7de1236f80-1/render-graph.json` | `8E4D4D27346976F13A1AF36FC2A71ECD7257FE828A724BD0C7B1D7B8D51C685C` |
| `CSX.BuildManifest.json` | `680A264E5E15F516436F10042461EFA04496AFB967B9AE1553BAFE9C436393FF` |
| DevBench runtime binding receipt | `44B64E58957D214496B8DC7763E3E077E7802CC9C3EF317C34F179B446ACCF50` |

## Next boundary

The next capture should use one fixed opaque Lighting object, followed by one
controlled Breezehome frame. Those captures can project render-pass,
technique, geometry, and bound-shader identity over the now-validated
versioned execution graph. Exact subresource overlap and observed D3D11 hazard
state should remain a separate slice rather than being inferred from those
semantic captures.
