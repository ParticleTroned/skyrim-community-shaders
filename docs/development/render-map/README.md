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
[`opaque-depth-culling-slice.md`](./opaque-depth-culling-slice.md).

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
existing CSX hook owners now emit bounded render-pass, technique-call, and
geometry-setup scopes when—and only when—an explicit controller has started a
capture. No additional detours or vtable replacements are installed. Immediate
render-pass coverage includes the three existing callsites and Terrain
Blending's shared draw route; geometry coverage in this slice is Lighting,
Effect, and Grass, matching the always-installed core hook owners.

There is intentionally no UI, DevBench command, or automatic capture trigger
yet, so ordinary game execution only performs the inactive check. Serialization,
gap-event materialization, per-thread buffer sharding, pointer-generation
tracking, remaining geometry families, and D3D draw/dispatch observation belong
to subsequent slices. Until those exist, this is live correlation plumbing,
not a complete capture producer.

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
