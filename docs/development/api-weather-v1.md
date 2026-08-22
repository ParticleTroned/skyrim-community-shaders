# Weather API v1

The weather API is a versioned controller beside the existing Weather Picker,
CS Editor, and legacy plugin interfaces. It preserves those paths unchanged.
Native consumers discover `include/VRAPI/CSweatherapi.h` through the `CSXR`
registry. DevBench exposes the same controller as
`communityshaders.weather_api` when that bridge and host are present.

## Contract and ownership

- Service: `csx.weather`; contract `1.0`; schema revision `1`.
- Native calls are main-thread-affine. Returned strings are borrowed until the
  next call to the same function on that thread and must be copied by callers.
- The interface and context live for the CSX process lifetime. Runtime
  unavailability is reported as state rather than by removing the service.
- TESWeather identities use load-order-portable `0xLOCAL~Plugin.esp` keys.
  Runtime form IDs are descriptive and must not be persisted by clients.
- This service owns runtime weather selection, preview/reset, weather lock,
  registered CSX weather variables, and CSX per-weather feature overrides.
- Arbitrary TESWeather record editing remains the CS Editor API's domain. This
  prevents the weather controller from silently acquiring record undo,
  inheritance, attachment, and reinitialization semantics it cannot guarantee.

Major versions are ABI-breaking. Additive future native shapes are named
`...002`; existing structures are not extended in place.

## Inspection model

`Snapshot001` reports structural control state: current/last/default/override
and locked weather keys, transition factor, lock-hook availability, save/load
mutation guards, catalog sizes, capabilities, and build identity.

`stateRevision` advances when structural weather control or effective override
state changes. It deliberately does not advance for every fractional transition
tick. This keeps optimistic concurrency useful while `transitionFactor` remains
a live observation.

The catalogs expose only authoritative data:

- Weather records: stable/editor/display/plugin identity, runtime and local form
  IDs, raw weather flags, and current role markers.
- Weather-enabled CSX features: loaded/paused state, registered-variable count,
  and current active-override count.
- Variables: names and descriptions, inferred JSON kind, current and captured
  user values, active ownership, and numeric range where the registered type
  provides one.
- Per-weather feature overrides: effective in-memory value and persisted file
  value separately. An editor-applied value can intentionally differ from disk.

The API does not manufacture an enum for feature-specific variable structures.
Their JSON representation and each registered variable's validator remain the
source of truth.

## Mutation safety

Every mutation uses `Preflight` followed by `Execute` with the exact same
arguments and the returned token. Tokens are single-use, expire after 30
seconds, and are invalidated by control-state changes. A non-zero expected
revision enables optimistic-concurrency checking; DevBench clients should
always use the latest snapshot revision.

| Action | Consent | Notes |
| --- | --- | --- |
| Set weather | disruptive | Normal override selection; may accelerate; retargets an active lock |
| Preview weather | disruptive | Transient `ForceWeather`; rejected while locked |
| Reset weather | disruptive | Releases selection to the climate; rejected while locked |
| Lock/unlock weather | disruptive when state changes | Runtime-only; lock reports whether detours or fallback enforcement are active |
| Pause/resume feature | disruptive | Relinquishes or reacquires registered weather-variable ownership |
| Reload overrides | disruptive | Restores controlled variables, reloads disk attachments, and reevaluates current weather |
| Set feature override | disruptive only with `applyLive` | Requires `persist`, `applyLive`, or both |
| Remove feature override | disruptive with `applyLive`; destructive with `persist` | Removes one feature entry, not the complete weather attachment |

Persistent mutation is rejected during CSX's save/load mutation-safety window.
Writes atomically replace the weather attachment, preserve unrelated top-level
and feature fields, and update only the selected feature entry. Persistence is
completed before an optional live application, so a failed write does not leave
the caller with an unreported live-only change.

Feature override objects require a boolean `__enabled` member. CSX normalizes
the object through the owning feature and then rejects unknown variable names,
type mismatches, non-finite numbers, and registered range violations. This
strict mutation boundary prevents misspellings from becoming inert persistent
configuration while read paths continue to preserve data they do not own.

## DevBench examples

All requests use the common envelope: `contractMajor`, `clientId`, `commandId`,
and `action`. `commandId` supplies idempotent replay. `expectedBuildId` can pin a
request to the intended loaded DLL.

```json
{
  "contractMajor": 1,
  "clientId": "weather-lab",
  "commandId": "snapshot-001",
  "action": "snapshot"
}
```

```json
{
  "contractMajor": 1,
  "clientId": "weather-lab",
  "commandId": "preflight-001",
  "action": "preflight",
  "mutation": {
    "action": "set_feature_override",
    "expectedStateRevision": 42,
    "weatherKey": "0x10A2~Skyrim.esm",
    "featureName": "IBL",
    "value": {
      "__enabled": true,
      "EnvIBLScale": 0.75
    },
    "persist": true,
    "applyLive": true,
    "allowDisruptive": true
  }
}
```

`execute` repeats the mutation exactly, adds `preflightToken`, and uses a new
envelope `commandId`. Inspection actions are `registry`, `snapshot`, `weathers`,
`features`, `variables`, and `override`.

## Deliberate boundaries

Weather API v1 does not edit cloud layers, colors, image spaces, precipitation
records, sounds, parent inheritance, or other TESWeather record fields. It does
not persist locks or runtime weather selection, delete entire attachment files,
or expose raw filesystem paths. Those omissions keep the API composable with
the future CS Editor service and make every destructive operation explicit.
