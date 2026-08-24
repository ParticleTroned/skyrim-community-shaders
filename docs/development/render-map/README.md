# CSX render map

The render map connects the deterministic CSX shader dependency manifest to
version-specific Skyrim rendering facts and bounded runtime observations. It is
a neighbouring system: it consumes stable shader-manifest identifiers, but does
not add machine-specific runtime state to the generated static manifest.

The system has four artefacts:

| Artefact | Authority | Lifetime |
|---|---|---|
| [`shader-manifest.generated.json`](../shader-analysis/shader-manifest.generated.json) | Deterministic CSX source and compile classification | Source revision |
| `engine-map.json` | Versioned Skyrim classes, functions, phases, techniques, and callsites | Executable build and map revision |
| `capture-manifest.json` plus `events.jsonl` | Bounded runtime evidence | One capture session |
| `render-graph.json` | Reproducible join of the preceding artefacts | Derived report |

The contracts are defined in [`schemas/`](./schemas/). The architecture and
correlation rules are in [`architecture.md`](./architecture.md). The first
vertical slice for depth culling is in
[`opaque-depth-culling-slice.md`](./opaque-depth-culling-slice.md). Findings
from the first bounded live controller run are in
[`live-capture-findings-2026-08-23.md`](./live-capture-findings-2026-08-23.md).
The later 120-frame run that validates durable finalization and establishes the
typed-observation gate is documented in
[`main-menu-120-frame-capture-2026-08-23.md`](./main-menu-120-frame-capture-2026-08-23.md).
Analysis joining that capture to the shader manifest, Skyrim's
`BSShader::BeginTechnique` lookup, the cache-key scheme, depth-culling results,
and residency baselines is in
[`existing-evidence-synthesis-2026-08-24.md`](./existing-evidence-synthesis-2026-08-24.md).
Its first non-example Skyrim VR engine-map seed is
[`engine-map.skyrim-vr-1.4.15.main-menu-seed.json`](./engine-map.skyrim-vr-1.4.15.main-menu-seed.json).

## Implementation status

The first C++ foundation is [`Collector.h`](../../../src/RenderMap/Collector.h).
A caller must start an explicitly bounded session before any record can be
accepted.

The foundation currently provides:

- fixed, preallocated event storage with frame, duration, event, and byte
  bounds;
- capture-generation-scoped observation IDs;
- thread-local frame, render-pass, technique, geometry, and command-list
  scopes;
- move-only guards that emit balanced begin/end events and clean up safely
  after out-of-order destruction;
- stop/start isolation, overflow accounting, and immutable stop snapshots.

The collector test covers lifecycle, all budget classes, nested correlation,
scope overflow and mismatch, stale guards, and concurrent writers.

The first live bridge is [`Runtime.h`](../../../src/RenderMap/Runtime.h). The
existing CSX hook owners emit bounded render-pass, technique-call, and
geometry-setup scopes when—and only when—an explicit controller has started a
capture. Immediate render-pass coverage includes the three existing callsites
and Terrain Blending's shared draw route; geometry coverage in this slice is
Lighting, Effect, and Grass, matching the always-installed core hook owners.
The D3D11 observer also tracks immediate-context VS/PS/CS bindings and bounded
draw/dispatch execution. Existing `Draw` and `DrawIndexed` hook owners compose
the observation directly; other draw variants and dispatch have dedicated
detours.

There is intentionally no UI or automatic capture trigger, so ordinary game
execution only performs the inactive check. The diagnostic DevBench service
`communityshaders.render_map` provides an
explicit, versioned `start` / `status` / `stop` lifecycle and paged
`capture_events` retrieval. It enforces one active capture, hard upper bounds,
exact capture IDs, idempotent commands, and a four-capture in-memory history.
Capture never starts automatically and event retrieval is unavailable until the
capture has stopped. Reaching any configured bound immediately quiesces the hot
hooks while retaining the session for explicit, idempotent finalization.

Finalization atomically commits `events.jsonl` followed by
`capture-manifest.json` beneath the SKSE log directory's
`CSX/RenderMapCaptures/<captureId>` folder. Both files are SHA-256 described in
the stop response and existing destinations are never overwritten. Event/byte
exhaustion produces an explicit terminal gap event and an incomplete manifest;
frame/time boundary triggers are reported separately and do not masquerade as
lost in-window events. Provenance that is unavailable in-process remains
explicitly unavailable rather than receiving a zero or invented hash.

The serializer emits schema-shaped event envelopes with semantic payload fields
and capture-local scope IDs. It preserves pointer evidence under the currently
supported `retain` policy, but leaves unproven manifest, engine, causal,
device-context, and eye relationships empty or unknown. Atomic event/manifest
writing, hashing, explicit gap materialization, and immediate finalization at a
capture boundary are implemented and live-validated.

Engine shader families and the selected vertex/pixel D3D shader objects now
have separately bounded, generation-scoped typed observation registries. The
v1.2 service emits the actual selection route, input and modified descriptors,
cache path for CSX-supplied objects, and creation-time bytecode SHA-256 when
available. Draws join the effective bound VS/PS observations; dispatches join a
compute-shader observation. Overflow of either identity catalogue makes the
capture explicitly incomplete. Per-thread buffer sharding, remaining geometry
families, provenance injection, render-target and full pipeline identity,
deferred-context/command-list coverage, and COM destruction boundaries remain
subsequent slices. Until those exist, this is bounded live evidence rather than
a complete render graph.

Example DevBench requests (use a unique `commandId` for each logical command):

```json
{"contractMajor":1,"clientId":"render-study","commandId":"start-1","action":"start","maxFrames":4,"maxDurationMs":2000,"maxEvents":8192,"maxShaderObservations":1024,"maxStageShaderObservations":4096}
{"contractMajor":1,"clientId":"render-study","commandId":"stop-1","action":"stop","captureId":"capture-live-..."}
{"contractMajor":1,"clientId":"render-study","commandId":"events-1","action":"capture_events","captureId":"capture-live-...","offset":0,"limit":500}
```

Validate the schemas, examples, stable shader/engine references, event ordering,
causal references, observation scopes, and derived graph references from the
repository root:

```powershell
pwsh -NoProfile -File tools/test-render-map-contracts.ps1
```

## Non-goals

- Replacing the shader dependency manifest.
- Treating a process pointer as a stable identifier.
- Guessing that a D3D11 call belongs to the globally most recent render pass.
- Capturing every draw indefinitely.
- Making a static reverse-engineering claim from one runtime observation.
- Implementing culling policy inside the evidence collector.

## Stable joins

External shader references use the existing `compile-####` and `pass-####`
identifiers. An engine map may relate multiple engine entities to multiple
shader-manifest records. Runtime events refer to capture-local observation IDs,
engine entity IDs, and shader-manifest IDs independently.

Every derived relationship retains its evidence. A consumer must be able to
distinguish source-proven, statically reverse-engineered, runtime-observed,
correlated, and validated-across-runs facts.

## Versioning

Each contract carries a `schema` object with a contract name, major version,
minor version, and producer version.

- A major version changes interpretation or required invariants.
- A minor version adds optional fields, event kinds, or enum values.
- Producers must not silently change the meaning of an existing field.
- Consumers reject unknown major versions.
- Consumers may accept a newer minor version only when they can preserve
  unknown `extensions` and tolerate unknown event kinds.
- Capabilities declare optional producer behaviour; version numbers do not.

Schema version 1 deliberately keeps runtime payload extensions open while the
event envelope and correlation rules remain strict.

## Repository policy

Schemas, small sanitized examples, engine-map facts, and derived compact reports
may be committed. Raw captures, addresses from unredistributable symbol data,
screenshots, frame dumps, and large binary artefacts belong in retained external
evidence storage. A committed report must identify the hashes of every input.
