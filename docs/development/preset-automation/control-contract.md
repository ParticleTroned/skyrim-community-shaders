# Feature control contract

Status: initialized from the 2026-08-17 preset-automation audit

## Objective

Every feature and performance-relevant setting must state when a requested change actually becomes authoritative. The UI and automation API must expose the same answer. A saved value is not evidence that the renderer is using it.

The long-term objective is to convert controls to safe live transitions wherever resource, hook, shader-permutation, and history ownership permit it. Until a transition is implemented and tested, its current boundary must remain explicit rather than being inferred by the caller.

## Mutability classes

| Class | Meaning | Required evidence before comparison |
| --- | --- | --- |
| `live` | Takes effect in the running renderer without changing location or process | Read back the effective value, wait the declared settle/history interval, then capture |
| `reload` | Requires a declared cell, save, renderer-resource, or equivalent bounded reload | Complete that reload, verify the effective value and scene identity, then capture |
| `restart` | Requires a new Skyrim process because startup hooks, feature loading, render-target allocation, shader defines, or equivalent boot contracts change | Restart through the matching settings and cache lane, verify startup state, then capture |

`unknown` is a valid temporary audit result and blocks unattended mutation. It must not silently fall back to `live`.

## Current audited distinctions

- The main feature-list switch changes the boot-disabled feature list. It is currently `restart` for every feature and does not unload or initialize a feature in the running process.
- IBL's inner `EnableIBL` setting is `live`: render-time state checks the setting and scene exclusions. It is distinct from disabling the complete IBL feature package in the feature list.
- Volumetric Shadows' inner `Enabled` setting is `live`: disabling it stops the copy/compute path and clears the shared shadow-map binding. It is distinct from disabling the complete feature package in the feature list.
- Known examples of genuine restart boundaries include VR Dynamic Cubemaps SSR enablement, SSGI resource-profile changes, VR Volumetric Lighting resource changes, selected upscaler/frame-generation resource options, and compute-shader define changes.
- A restart used merely because automation can only replace an on-disk settings file is a harness limitation, not evidence that the underlying renderer control is restart-bound.

This list is a seed audit, not a complete inventory. Each feature dossier should link its audited controls here or carry the equivalent structured records.

## UI contract

Every control must display its mutability beside the control rather than only after the user changes it. The presentation should distinguish:

- **Live** — includes any history reset or visible settling period;
- **Reload required** — names the required reload boundary; and
- **Restart required** — names the boot/resource/cache reason.

When a saved value differs from the effective runtime value, the UI must show both and retain a visible pending-state indicator. A feature-list boot toggle must not visually resemble an inner live enable switch without the restart label.

## Automation API contract

Capability discovery must expose control metadata before mutation. At minimum, each control record needs:

```json
{
  "feature": "ImageBasedLighting",
  "control": "EnableIBL",
  "valueType": "boolean",
  "requestedValue": true,
  "effectiveValue": true,
  "mutability": "live",
  "settle": {
    "kind": "frames",
    "minimum": 2,
    "requiresMenuClose": false,
    "resetsHistory": false
  },
  "cacheImpact": "none",
  "resourceImpact": "retained",
  "canRestoreInSession": true,
  "available": true,
  "unavailableReason": null
}
```

Mutation responses must return the accepted requested value and the effective value separately. A live request is complete only when readback matches and its settle contract has elapsed. Reload/restart requests may be staged, but the API must report them as pending and must never claim that the renderer changed immediately.

The first automation controls should be the audited live inner switches for IBL and Volumetric Shadows. General arbitrary JSON rewriting is not an acceptable substitute for typed validation and effective-state readback.

The Release+DevBench build now exposes those first controls through `communityshaders.controls`. `list` returns the writable live controls together with their package-level restart boundary; `get` reads one record; and `set` accepts a typed Boolean value. Mutations are deliberately session-only so an interrupted experiment cannot silently rewrite the user's preset:

```json
{"action":"list"}
```

```json
{"action":"set","feature":"ImageBasedLighting","control":"EnableIBL","value":false}
```

```json
{"action":"set","feature":"VolumetricShadows","control":"Enabled","value":true}
```

Every response distinguishes the setting's effective value from `runtimeActive`, which may additionally depend on scene state such as an IBL interior exclusion or the presence of directional shadows. Package-level `packageEnabled` records are read-only and report `restart`; they must not be confused with the live inner controls.

Commit `9218ec2e8` extends the same tool with `performanceActive` for every feature that implements CSX's production Performance Tuning measurement contract. This is a reversible ablation surface, not arbitrary settings mutation:

```json
{"action":"set","feature":"Skylighting","control":"performanceActive","value":false}
```

The first disable stores the feature's complete measurement state in memory and applies the feature's own off transition. A later `value:true` restores that snapshot when one is held; if no snapshot exists it applies the feature's production measurement-on default. Callers must inspect `restoredSnapshot` and effective readback because restoring a baseline that was already off can legitimately remain off. `restoreAll` is the session safety operation:

```json
{"action":"restoreAll"}
```

Each record includes per-direction settle seconds, readiness, wait text, menu-close requirements, history-reset disclosure, cache impact, resource impact, and whether a snapshot is outstanding. The loaded shader set remains retained, but feature-specific runtime resources may be reset or recreated. The initial native-OpenVR null-HMD validation discovered 15 performance surfaces, 14 available in the selected loadout. Grass Collision and Skylighting completed on/off/restore transitions with effective readback and zero remaining snapshots; this validates the bridge path, not every feature's visual or lifetime correctness.

## Promotion toward live transitions

Converting a control to `live` requires feature-specific proof that enable, disable, and repeated A/B/A transitions:

1. create or retain every required resource safely;
2. detach stale SRVs/UAVs, hooks, constant state, and downstream consumers;
3. reset temporal history where required;
4. preserve stereo correctness and loading-transition safety;
5. restore the exact starting state without increasing retained memory or queue debt; and
6. produce stable effective-state readback for the UI and API.

Controls that would make the idle feature retain substantial VRAM may remain `reload` or `restart`, or may expose an explicit resource-retention policy. “All live” is the design target, but correctness, bounded memory, and deterministic restoration remain hard constraints.
