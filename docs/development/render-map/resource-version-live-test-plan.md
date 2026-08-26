# Resource-version graph live-test gate

Date prepared: 2026-08-26

Outcome: **passed**. The live result and retained artifact hashes are recorded
in
[`resource-version-main-menu-capture-2026-08-26.md`](./resource-version-main-menu-capture-2026-08-26.md).

## Scope

This protocol is the first live gate for `resource-versions-1`. It validates
the capture mechanism and derived graph at Skyrim VR's deterministic main menu.
It does not classify world-scene resources and does not claim exact
subresource-overlap or deferred-context coverage.

No MO2, SteamVR, or Skyrim process was launched while preparing this gate.

## Pre-live evidence

The retained one-frame capture at
`L:\Codex\evidence\render-map\20260826-main-menu-resource-flow` was replayed
offline through the new joiner. The output validated against
`render-graph.schema.json` and contained:

| Item | Count |
|---|---:|
| Nodes | 117 |
| Edges | 283 |
| Reads | 85 |
| Writes | 54 |
| Allocation-to-version ownership edges | 71 |
| RAW dependencies | 24 |
| WAR dependencies | 4 |
| WAW dependencies | 45 |
| Effective-state adjustments | 0 |
| Explicit capability gaps | 1 |

The only gap is the intentional non-blocking statement that resource versions
and hazard overlap are allocation-wide. The graph is acyclic. The offline
result is a compatibility check against the old event stream; only a new live
capture can exercise event schema minor 8 and the added mutation hooks.

## Deployment and capability gate

1. Build with the canonical `CSmain` wrapper and record its receipt, source
   revision/dirty state, DLL SHA-256, and build-manifest identity.
2. Deploy that exact DLL to a task-owned, winning MO2 mod. Do not replace or
   reorder unrelated mods.
3. Start the selected VR route and Skyrim through an owned MO2 session.
4. Query `communityshaders.render_map` server information before capture.
5. Require contract major 1, contract minor at least 6, schema revision at
   least 7, and capability `resource-mutation-observations`. Refuse the live
   test politely if any requirement is absent.

## Capture

At the stable main menu, start one explicitly bounded capture with:

- one CPU frame;
- 1,000 ms maximum duration;
- 65,536 events and 32 MiB event storage;
- 8,192 shader observations;
- 32,768 stage-shader, resource, target-view, and target-binding observations.

Use unique client and command IDs. Allow the frame boundary to finish the
capture, then issue an idempotent explicit stop/status request if needed. Retain
the atomic `capture-manifest.json` and `events.jsonl`; do not use paged API
responses as the authoritative artifact when final files are available.

## Acceptance checks

1. Capture status is complete, not truncated, with no dropped event or
   observation catalogue entries.
2. Event envelopes report schema minor 8.
3. Resource mutation events, if naturally exercised at the main menu, use only
   the documented operation names and nullable source identity for
   destination-only operations.
4. `build-render-graph.py` emits schema 1.2 with producer
   `resource-versions-1` and `csx.graphAcyclic=true`.
5. Every used allocation has version 0; each execution/allocation write pair
   creates exactly one later version; every read targets the current version at
   that event.
6. Every `precedes` dependency cites forward-ordered capture events and carries
   exactly one of RAW, WAR, or WAW with `correlated` evidence.
7. `csx.effectiveStateAdjustments` is recorded. If non-zero, inspect the
   relevant bind sequence and confirm that no suppressed SRV becomes a read
   edge while the same allocation is an output.
8. The only expected gap is the non-blocking allocation-wide
   subresource/hazard limitation. Any missing declaration, incomplete capture,
   or undeclared-resource gap fails the gate.

## After the main-menu mechanism gate

Do not immediately collect a large free-running world trace. First capture one
fixed opaque Lighting object, then one controlled Breezehome frame. Those
captures should project render-pass, technique, geometry, and selected-shader
identity over the lossless versioned execution graph. Exact subresource overlap,
indirect argument-resource reads, map/unmap, deferred contexts, command-list
replay, and eye attribution remain separate slices.
