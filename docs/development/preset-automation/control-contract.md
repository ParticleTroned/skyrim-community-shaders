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
    "minimumFrames": 2,
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

The timing and visual ablation runners accept an explicit control name rather
than assuming `performanceActive`. They now consume both settle-contract shapes:
package measurement controls expose readiness plus per-direction seconds, while
inner live switches expose `minimumFrames`. For the latter, the runners verify
the requested effective Boolean after the declared frame count and always
restore the exact starting Boolean even though these controls do not use the
quality/performance snapshot registry. Optional expected cell, worldspace,
weather, hour, and yaw values turn scene readback mismatches into rejected runs.

Commit `9218ec2e8` extends the same tool with `performanceActive` for every feature that implements CSX's production Performance Tuning measurement contract. This is a reversible ablation surface, not arbitrary settings mutation:

```json
{"action":"set","feature":"Skylighting","control":"performanceActive","value":false}
```

The first disable stores the feature's complete measurement state in memory and applies the feature's own off transition. A later `value:true` restores that snapshot when one is held; if no snapshot exists it applies the feature's production measurement-on default. Callers must inspect `restoredSnapshot` and effective readback because restoring a baseline that was already off can legitimately remain off. `restoreAll` is the session safety operation:

```json
{"action":"restoreAll"}
```

Each record includes per-direction settle seconds, readiness, wait text, menu-close requirements, history-reset disclosure, cache impact, resource impact, and whether a snapshot is outstanding. The loaded shader set remains retained, but feature-specific runtime resources may be reset or recreated. The initial native-OpenVR null-HMD validation discovered 15 performance surfaces, 14 available in the selected loadout. Grass Collision and Skylighting completed on/off/restore transitions with effective readback and zero remaining snapshots; this validates the bridge path, not every feature's visual or lifetime correctness.

The response-curve extension adds `qualityProfile` enum controls for the inherited Performance/Balanced/Quality clusters in Skylighting, Screen Space Shadows, and Wetterness:

```json
{"action":"set","feature":"Skylighting","control":"qualityProfile","value":"Performance"}
```

The first profile selection holds the complete feature state; later selections in the same sweep retain that original snapshot. `restore` returns one feature to the exact baseline, while `restoreAll` remains the interruption-safety path. A feature cannot hold a `performanceActive` and `qualityProfile` snapshot simultaneously.

Records expose the profile definitions and effective parameter readback. Mutability is feature-specific:

- Skylighting recreates probe-grid resources, resets its history, and reports readiness after a five-second settle;
- Wetterness resets owned temporal weather state and reports a five-second settle; and
- Screen Space Shadows may release and compile new per-eye raymarch variants, reports `live-recompile-settle`, and is ready only when the required compiled sample counts match the selected profile.

The first runtime proof switched all three features across profiles and restored the original Quality state with zero outstanding snapshots. At the null-HMD render size, Screen Space Shadows produced cached per-eye variants with 12, 23, and 33 compiled samples for Performance, Balanced, and Quality respectively.

Screen Space Shadows also exposes a bounded `qualityParameters` object for attributable calibration. Fields are optional but at least one is required; unknown, non-numeric, non-finite, or out-of-range values are rejected rather than silently normalized:

```json
{"action":"set","feature":"ScreenSpaceShadows","control":"qualityParameters","value":{"VRBaseSamplesAtReference":30,"VRCullDistance":0}}
```

`VRBaseSamplesAtReference` accepts `16`–`96`; `VRCullDistance` accepts `0`–`20480`, with zero disabling distance culling. The control shares the feature's original-state snapshot with `qualityProfile`, so a calibration may alternate named profiles and partial parameter overrides before restoring either control. Effective readback includes the requested settings and compiled left/right sample counts. This permits a range-only comparison at a fixed sample count and a sample-only comparison at a fixed range without creating artificial preset names. It retains the same `live-recompile-settle` and runtime shader-cache disclosures as the profile control.

Wetterness exposes the same reversible `qualityParameters` shape for its seven
tiered numeric controls:

```json
{"action":"set","feature":"Wetterness","control":"qualityParameters","value":{"RaindropGridSize":3.6,"RaindropInterval":0.65,"RaindropChance":0.6}}
```

The fields are `RaindropFxRangeWorldUnits`, `WetnessDistanceFadeRange`,
`RaindropGridSize`, `RaindropInterval`, `RaindropChance`, `SplashesLifetime`,
and `RippleLifetime`. Bounds follow the production UI/sanitizer contract. The
material fade therefore cannot be requested below `7002.801` game units (100
metres); out-of-range requests are rejected before mutation rather than
silently producing a different effective tier. Readback additionally reports:

- material fade range in metres;
- the raindrop full-strength and cutoff radii implied by the lighting shader;
  and
- the cell-time opportunity rate `chance / (gridSize² × interval)`.

Those derived values describe coverage and candidate event density, not visible
raindrop count or GPU cost. Rain intensity, occlusion, surface orientation,
radius tests, and the splash/ripple lifetime functions still determine the
real contribution. Every change resets Wetterness-owned temporal state and
retains the five-second settle gate, but uses the already loaded shaders and
does not compile a new variant. Runtime proof rejected a `7000`-unit fade,
accepted a partial chance mutation, and restored the exact original seven-field
state with no outstanding snapshot at Info logging.

## Promotion toward live transitions

Converting a control to `live` requires feature-specific proof that enable, disable, and repeated A/B/A transitions:

1. create or retain every required resource safely;
2. detach stale SRVs/UAVs, hooks, constant state, and downstream consumers;
3. reset temporal history where required;
4. preserve stereo correctness and loading-transition safety;
5. restore the exact starting state without increasing retained memory or queue debt; and
6. produce stable effective-state readback for the UI and API.

Controls that would make the idle feature retain substantial VRAM may remain `reload` or `restart`, or may expose an explicit resource-retention policy. “All live” is the design target, but correctness, bounded memory, and deterministic restoration remain hard constraints.
