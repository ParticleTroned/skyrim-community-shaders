# Shader API v1

The shader API adds a versioned controller beside the existing CSAP and CSX
menu paths. It does not remove, reinterpret, or add methods to the legacy
interfaces. The native contract is `include/VRAPI/CSshaderapi.h`; DevBench
exposes the same controller as `communityshaders.shader_api` when the bridge is
built and a host is present.

## Scope and compatibility

- Service name: `csx.shader`; contract `1.0`; schema revision `1`.
- Native discovery uses the `CSXR` registry described in
  [API service registry](api-service-registry.md).
- A major version is an ABI break. Minor versions may add separately sized
  structures or function-table members, but v1 structures are not extended in
  place. A future shape is named `...002` so an old caller is never overwritten.
- Function tables and their context live for the CSX process lifetime. A
  service whose game state is not initialized reports `unavailable` rather than
  disappearing.
- Native calls are main-thread-affine. Borrowed strings must be copied before
  the next call to the same function on that thread.
- The DevBench adapter schedules work on the SKSE main task queue and has a
  bounded five-second wait. It never exposes renderer objects, shader blobs, or
  cache filesystem paths.

## State model

`Snapshot001` separates states that the old UI could make easy to conflate:

- custom-shader **requested**, **effective**, and transition-pending state;
- disk-cache **requested**, **active**, held-for-decision, and rollback state;
- feature-set change and rollback-pending state;
- compilation progress, failures, cache hits, source compiles, slow work, work
  in flight, and foreground/background worker counts;
- save/load safe mode and whether persistent mutation is currently blocked;
- canonical build ID, shader-cache ABI ID, and shader-compiler identity.

`stateRevision` changes when controllable state changes. Mutations carry the
revision observed by the caller; a stale non-zero revision is rejected. A zero
revision deliberately opts out of optimistic-concurrency checking in the native
ABI. DevBench clients should always send the snapshot revision.

The feature catalog reports installed feature objects and only facts CSX can
currently establish: load result, saved disabled-at-boot state, missing-runtime-
dependency suppression, core/menu/hidden/VR flags, category, and shader define.
`loaded` is startup lifecycle state; it is not a promise that every pass owned by
the feature executes in the current frame.

This catalog intentionally does **not** invent a complete render graph. A
feature may contribute engine-shader permutations, independent compute or image
passes, overlays, resources consumed by another feature, or more than one of
those. CSX does not yet have authoritative machine-readable input/output and
dependency metadata for every feature. That classification belongs in a future
additive graph/catalog contract after the feature declarations can be made the
source of truth.

## Mutations and safety

Every mutation uses two calls:

1. `Preflight` validates availability, revision, feature/cache lifecycle, and
   consent flags, then returns a token bound to the exact request for 30 seconds.
2. `Execute` presents that token and the same arguments. CSX revalidates state,
   consumes the token, performs the operation, and returns a receipt plus the new
   revision.

Tokens are single-use. Any observed state change invalidates outstanding tokens.
The receipt distinguishes a live change from completed persistence. If a save
fails, status is `persistence_failed`, `applied` remains true, and `persisted` is
false so a controller cannot mistake a live-only change for durable state.

| Action | Consent | Persistence | Lifecycle result |
| --- | --- | --- | --- |
| Set custom shaders | disruptive | optional | may defer VR disable until native targets are restored; enabling may compile |
| Set disk cache | none | optional | runtime setting |
| Set async compilation | none | optional | runtime setting |
| Set skip unchanged | none | optional | runtime setting |
| Set feature disabled at boot | none | optional | restart and recompile expected |
| Clear memory cache | disruptive | not applicable | recompile expected |
| Clear disk cache | disruptive + destructive | not applicable | resets managed A/B packs in place (or removes legacy active/rollback caches); recompile expected |
| Clear all caches | disruptive + destructive | not applicable | clears memory and resets/removes disk caches; recompile expected |
| Restore previous disk cache | disruptive + destructive | internal transaction | only while idle and a compatible previous cache exists; restart required |
| Accept cache rebuild | disruptive | not applicable | only while the cache is held for a rebuild decision |
| Stop compilation | disruptive | not applicable | bounded stop request |
| Capture active shaders | disruptive | not applicable | asynchronous capture begins and receipt is pending |

`persist` is meaningful only for the four general settings and feature boot
state. It saves through the existing atomic CSX settings path. Preflight rejects
persistence during the engine save/load mutation-safety window, while an
explicit live-only change remains available.

## DevBench request shape

Every request participates in the common envelope and therefore includes
`contractMajor`, `clientId`, `commandId`, and `action`. `commandId` provides
idempotent replay through the common service foundation.

Inspection example:

```json
{
  "contractMajor": 1,
  "clientId": "shader-lab",
  "commandId": "snapshot-001",
  "action": "snapshot"
}
```

Mutation preflight example:

```json
{
  "contractMajor": 1,
  "clientId": "shader-lab",
  "commandId": "preflight-001",
  "action": "preflight",
  "mutation": {
    "action": "clear_memory_cache",
    "expectedStateRevision": 42,
    "allowDisruptive": true
  }
}
```

Execute repeats the mutation, adds `preflightToken`, uses a new `commandId`, and
sets action to `execute`. Callers can additionally pass `expectedBuildId`; a
mismatch is rejected before domain work runs.

## Deliberate boundaries

The API preserves the existing menu and CSAP behavior. It does not currently:

- hot-unload an individual feature whose lifecycle is startup-bound;
- expose raw compiled shader binaries, arbitrary cache paths, or shader-source
  mutation;
- claim that a saved disabled feature is already absent from this process;
- classify every pipeline, independent overlay, or resource input/output;
- compose active-shader capture into a video or profiling sequence.

Those omissions are safety and truthfulness boundaries, not permanent format
limitations. Future services can reference this catalog by stable short name
and build/cache provenance without changing v1 behavior.
