# Opaque Lighting Breezehome capture, 2026-08-26

## Result

The first controlled world-scene capture passed the bounded one-frame render
graph gate in Breezehome. It proves that the live collector can correlate
render-pass scopes, geometry pointers, selected Lighting shader objects, D3D11
resource/view state, and draw execution in a genuine loaded cell.

It does **not** yet prove the identity of one named scene object or material.
The runtime contract has no `object-observed` or material/property event and no
geometry/shader-family capture filter. A three-frame unfiltered attempt reached
the hard 65,536-event ceiling after two complete frames. The correct next slice
is targeted semantic capture, not a claim that a process pointer is an object
identity.

## Runtime and scene identity

The task-owned MO2 profile loaded the same exact artifact used by the preceding
resource-version gate:

| Identity | Value |
|---|---|
| Skyrim runtime | `SkyrimVR.exe` 1.4.15 |
| Cell | `WhiterunBreezehome` (`0x000165A8`) |
| Location | `WhiterunBreezehomeLocation` |
| Source commit | `00551cf3af4d84d3c3447c22c72e7f1e23d90576` (dirty) |
| Build ID | `ff9fbf8438d61d9619b0ab5c2ab8d750804c6b3a2393cb12a7e16d53f3602221` |
| DLL SHA-256 | `35506120B3134025BB9EADF5062175AC0A1A6F237E3ED67A516D8613B23DD4B7` |
| Render-map contract | Major 1, minor 6, schema revision 7 |
| Event schema | Major 1, minor 8 |

The save fixture was copied from the stable profile and its ESS/SKSE hashes
were verified before launch. After the asynchronous load, DevBench confirmed
the Breezehome cell, `playerLoaded=true`, and only `HUD Menu` open. The runtime
route was OCU/Virtual Desktop; SteamVR was not running.

## Complete one-frame capture

Capture `capture-live-0000314145be5778-2` completed on CPU frame `24088` with:

- 21,394 accepted events and zero dropped events;
- no truncation, catalogue overflow, scope mismatch, or scope overflow;
- 625 resource, 645 target-view, and 27 target-binding observations;
- 19 engine-shader and 133 selected-stage-shader observations;
- one expected boundary rejection at the frame-limit edge; and
- complete atomic manifest and event artifacts.

The event distribution demonstrates that this is a full world-scene frame:

| Event kind | Count |
|---|---:|
| Draw | 5,865 |
| Resource-view bind | 5,429 |
| Render-pass enter / exit | 3,397 / 3,397 |
| Geometry-setup begin / end | 560 / 560 |
| Technique begin / end / resolved | 172 / 172 / 172 |
| Resource flow | 70 |
| Dispatch | 19 |

## Lighting correlation

The capture observed `Lighting` as an engine shader family and resolved 28
Lighting stage-shader identities from `Data/ShaderCache/Lighting`.

| Correlation | Count |
|---|---:|
| Draws with both vertex and pixel stages resolved to Lighting | 503 |
| Distinct render-pass geometry pointers for those draws | 493 |
| Lighting stage-shader observations | 28 |

Representative correlated draws use:

- `Data/ShaderCache/Lighting/0.vso` with
  `Data/ShaderCache/Lighting/11.pso` for technique `1207976045`; and
- `Data/ShaderCache/Lighting/0.vso` with
  `Data/ShaderCache/Lighting/19.pso` for technique `1207976053`.

Those draws are runtime-observed Lighting-family evidence. The geometry pointer
is valid only within this process and capture generation; it is not promoted to
a stable object, mesh, form, or material identifier.

## Derived graph

`tools/build-render-graph.py` joined the complete capture to the deterministic
shader manifest and Skyrim VR 1.4.15 engine-map seed. The resulting schema 1.2
graph is acyclic:

| Item | Count |
|---|---:|
| Nodes | 13,228 |
| Edges | 45,851 |
| Resource nodes | 7,274 |
| Draw nodes | 5,865 |
| Resource-operation nodes | 42 |
| Copy nodes | 28 |
| Dispatch nodes | 19 |

The graph reports 4,780 non-blocking `uncorrelated` gaps, principally draws
whose output-merger or resource state was inherited before the capture began,
and one declared `unsupported-route` gap. It reports no ambiguity groups and
records `csx.graphAcyclic=true`. Deferred contexts and VR-eye attribution remain
explicitly unsupported; these are capability boundaries, not inferred facts.

## Multi-frame bound discovery

Capture `capture-live-0000311c6a6c057c-1` requested three frames but reached
the service's hard event limit:

- 65,536 accepted events plus one synthetic terminal gap;
- completion reason `event-limit`, `truncated=true`;
- two complete CPU frames with Lighting evidence (`19070` and `19071`);
- 513 and 509 Lighting draws respectively; and
- 497 geometry pointers present in both captured frames.

This trace is retained as capacity and repeatability evidence only. It is not a
lossless render graph and is not used to satisfy the one-frame graph gate.

## Shader-cache observation

All four `.csxpack` files remained byte-identical during the live run. The
working-tree hash changed only because `Info.ini` recorded the active build.
The cache controller preserved that unverified working tree and restored the
exact pre-run tree hash
`017243828640A977BBA985866887DC290D6E952BB92EAB05F42BB6DD5CE6AE0F`.
This again confirms that changing only the DLL build identity did not cause a
shader-pack rebuild.

## Retained evidence

Authoritative artifacts are retained at
`L:\Codex\evidence\render-map\20260826-opaque-lighting-live`.

| Artifact | SHA-256 |
|---|---|
| Complete capture manifest | `93DAFAB6B9FE1D7C8B61D6F7672F9945AD9C5B53D1974DAE66D33328A77F22C3` |
| Complete capture events | `0F879CD5358D2CF8B898CE86D843C9AF90205D0B53F869D282A422AC59FE5762` |
| Complete derived graph | `F6EFE35F8BD5D0D23432628E5943716F9FCB55FFB567A429D1F99E943485271B` |
| Event-limited manifest | `D59A784783A7917982116F589101C49AED5C0F5B1E3CF4A333C724BF4C59197B` |
| Event-limited events | `5DFA53F4861DCFC73A2CE169D0FB73F0CF7D3ECBAE568DA7865A43B4152096EC` |
| Event-limited derived graph | `FB85B6D6B84E9EBD18D82492D53367C2324B65A93B03EAD77839EE31D7EFD19D` |

## Next boundary

Add an opt-in semantic/targeted capture slice that can:

1. declare a scene object, geometry, shader property/material, and stable
   capture-local relationships among them;
2. filter hot-path events to one selected geometry/object or at least the
   Lighting family; and
3. retain that target across three consecutive frames with explicit eye and
   submission attribution where available.

Only then should the render map claim a fixed opaque Lighting object across
frames. Raising the global event ceiling alone would make captures larger
without solving object identity or correlation precision.
