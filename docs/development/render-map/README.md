# CSX render map

The render map connects the deterministic CSX shader dependency manifest to
version-specific Skyrim rendering facts and bounded runtime observations. It is
a neighbouring system: it consumes stable shader-manifest identifiers, but does
not add machine-specific runtime state to the generated static manifest.

The system has five artefacts:

| Artefact | Authority | Lifetime |
|---|---|---|
| [`shader-manifest.generated.json`](../shader-analysis/shader-manifest.generated.json) | Deterministic CSX source and compile classification | Source revision |
| [`prior-art-catalog.json`](./prior-art-catalog.json) | Pinned public candidate claims with explicit engine/shader applicability and target verification state | Source revisions plus target identity |
| `engine-map.json` | Versioned Skyrim classes, functions, phases, techniques, and callsites | Executable build and map revision |
| `capture-manifest.json` plus `events.jsonl` | Bounded runtime evidence | One capture session |
| `render-graph.json` | Reproducible join of the preceding artefacts | Derived report |

The contracts are defined in [`schemas/`](./schemas/). The architecture and
correlation rules are in [`architecture.md`](./architecture.md). The first
public-source catalogue, promotion boundary, and efficient spot-check order are
in [`prior-art-integration.md`](./prior-art-integration.md). The first
vertical slice for depth culling is in
[`opaque-depth-culling-slice.md`](./opaque-depth-culling-slice.md).
The device-context and command-list identity/state contract required for honest
deferred-context coverage is in
[`device-context-command-list-slice.md`](./device-context-command-list-slice.md).
The immediate-context render-target/depth-target identity and draw-join slice is
in [`output-merger-target-slice.md`](./output-merger-target-slice.md).
Its bounded main-menu live-validation record is in
[`output-merger-main-menu-capture-2026-08-26.md`](./output-merger-main-menu-capture-2026-08-26.md).
The typed resource/view and producer-consumer graph slice is in
[`resource-flow-graph-slice.md`](./resource-flow-graph-slice.md).
The next graph-correctness slice versions resource contents by observed write,
derives conservative RAW/WAR/WAW dependencies, and separates directly observed
API state from conservative D3D11 hazard reconstruction. It deliberately uses
allocation-wide overlap until exact view-subresource overlap is proved.
Its prepared no-launch validation protocol is in
[`resource-version-live-test-plan.md`](./resource-version-live-test-plan.md).
The bounded main-menu live gate passed; its exact runtime identity, capture
counts, version/dependency invariants, cache observation, and retained hashes
are recorded in
[`resource-version-main-menu-capture-2026-08-26.md`](./resource-version-main-menu-capture-2026-08-26.md).
The following controlled Breezehome frame confirms genuine world-scene
Lighting/render-pass/geometry/resource correlation and records the exact
targeted semantic-capture boundary exposed by the live event density:
[`opaque-lighting-breezehome-capture-2026-08-26.md`](./opaque-lighting-breezehome-capture-2026-08-26.md).
Findings from the first bounded live controller run are in
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
The D3D11 observer also tracks immediate-context shader bindings and bounded
draw/dispatch execution. It records immediate-context output-merger
render-target/depth-target calls as bounded, capture-local target-view and exact
binding-set identities, typed resources and RTV/DSV/SRV/UAV views, ordered
per-slot SRV/UAV state, and copy/resolve flow. Draws join the most recently
observed binding set. `Draw`, `DrawIndexed`, other draw variants, dispatch,
copy, and resolve have bounded observation paths. On first observed context
activity in each capture it emits one typed
`device-context-observed` declaration. Later execution events reference that
capture-local identity and carry a monotonic context-local command sequence;
the sequence also advances across observed VS/PS/CS binding calls so relative
state-to-execution order is preserved without emitting an event for every bind.

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
supported `retain` policy, but leaves unproven manifest, engine, causal, and
per-draw eye relationships empty or unknown. Atomic event/manifest
writing, hashing, explicit gap materialization, and immediate finalization at a
capture boundary are implemented and live-validated.

Engine shader families and the selected vertex/pixel D3D shader objects now
have separately bounded, generation-scoped typed observation registries. The
v1.9 service emits the actual selection route, input and modified descriptors,
cache path for CSX-supplied objects, and creation-time bytecode SHA-256 when
available. Engine observations and stage aliases also retain the effective
HLSL compile-source name, so shared ImageSpace families join the static shader
manifest without assuming that each runtime loader alias names a separate
source file. Draws join the effective bound VS/PS observations and the last
output-merger target binding observed after capture began; dispatches join a
compute-shader observation. Overflow of any identity catalogue makes the
capture explicitly incomplete. Lightweight VS/PS/CS bytecode identity is
retained at D3D creation independently of `Dump Shaders`, and the first
captured execution lazily declares stage objects already bound when capture
started. Skyrim's shader-load boundary also records the loader family and
descriptor aliases attached to each loaded D3D object. Up to eight exact
aliases are retained per stage observation, with the total and truncation
state explicit rather than collapsing shared bytecode or wrapper aliases.
Other inherited state is not queried or guessed, so early draws may
still correctly carry a null target-binding identity until the first observed
bind. Per-thread buffer sharding, remaining
geometry families, provenance injection, the rest of full pipeline identity,
deferred-context/command-list coverage, COM destruction boundaries, and general
D3D11 hazard reconstruction remain subsequent slices. The depth visibility SRV
has targeted effective-binding verification, and accepted OpenVR submissions
now provide resource-and-bounds eye attribution. Earlier draws acquire an eye
label only through a proven resource-flow path; a combined stereo draw may
legitimately resolve to `both`.
The resource-flow joiner now produces a genuine bounded immediate-context
execution/resource graph and projects exact capture-local render-pass,
technique, observed-geometry, temporal geometry-setup, engine-shader, and
selected stage-shader identities onto execution nodes. Projection uses
explicit scope and observation IDs only;
pointer equality is not treated as identity. It remains an incomplete semantic
Skyrim frame graph because externally verified static manifest/engine
references are not yet present in live events. Scene-object, geometry, and
material-state identities are now present and joined through exact
capture-local observation IDs.

The depth-culling integration, versioned draw-output to accepted-eye join, and
next live capture target are described in
[`depth-culling-observation-sequence.md`](./depth-culling-observation-sequence.md).
The target-specific static bridge through Skyrim VR's culling hierarchy,
shader accumulator, and batch renderer—plus the still-unresolved world
scene-list producer and timing-contamination gate—is recorded in
[`scene-accumulation-culling-static-slice.md`](./scene-accumulation-culling-static-slice.md).
The first loaded-scene live join of the OBB shader, typed buffers and views,
stereo instanced draw, result publication, staging and reset copies,
visibility consumers, and following-frame CPU readback decisions is recorded
in
[`obb-resource-live-capture-2026-08-29.md`](./obb-resource-live-capture-2026-08-29.md).
The first accepted OpenVR eye-submission capture, including the exact
side-by-side texture identity and per-eye bounds under the Valve null-HMD route,
is recorded in
[`eye-submission-live-capture-2026-08-29.md`](./eye-submission-live-capture-2026-08-29.md).
The bounded main-menu proof that runtime ImageSpace aliases retain their actual
shared HLSL compile-source identity, and therefore join to the correct static
compile units without name guessing, is recorded in
[`imagespace-compile-source-main-menu-capture-2026-08-29.md`](./imagespace-compile-source-main-menu-capture-2026-08-29.md).
The next generic semantic-identity slice—scene reference, geometry, shader
property and material state, plus the exact setup binding that connects those
catalogues to a draw—is specified in
[`object-material-identity-slice.md`](./object-material-identity-slice.md).
Its first bounded Lighting-only Breezehome live gate, including exact hashes,
zero-loss completion, selector behaviour, semantic counts, and the remaining
material-revision test, is recorded in
[`object-material-lighting-live-capture-2026-08-29.md`](./object-material-lighting-live-capture-2026-08-29.md).
The follow-up 120-frame Effect capture, including the live negative revision
result and the distinction between stable materials and churning particle
geometry, is recorded in
[`effect-material-revision-live-capture-2026-08-29.md`](./effect-material-revision-live-capture-2026-08-29.md).
The next read-only dependency slice records bounded runtime material texture
roles and joins each binding to its exact capture-local D3D11 resource
observation. Its index semantics, revision rules, limits, and live gate are in
[`material-texture-dependency-slice.md`](./material-texture-dependency-slice.md).
The completed Lighting and Effect Breezehome gate, including exact binding,
path, resource, graph and controller-budget findings, is recorded in
[`material-texture-live-capture-2026-08-29.md`](./material-texture-live-capture-2026-08-29.md).
The following graph-only slice joins those exact material resource identities
to the ordered stage/slot SRV state effective at draw execution, without
equating material-list positions with shader registers. Its contract and next
live gate are in
[`material-input-execution-correlation-slice.md`](./material-input-execution-correlation-slice.md).
The first retained world-scene re-derivation with semantic identity projection
is recorded in
[`semantic-projection-breezehome-analysis-2026-08-27.md`](./semantic-projection-breezehome-analysis-2026-08-27.md).
The safe capture-start contract for the remaining stable build, manifest,
environment, cache, and scenario identity gap is defined in
[`capture-provenance-contract.md`](./capture-provenance-contract.md).

Example DevBench requests (use a unique `commandId` for each logical command):

```json
{"contractMajor":1,"clientId":"render-study","commandId":"start-1","action":"start","maxFrames":4,"maxDurationMs":2000,"maxEvents":8192,"maxShaderObservations":1024,"maxStageShaderObservations":4096,"maxResourceObservations":4096,"maxTargetViewObservations":4096,"maxTargetBindingObservations":4096}
{"contractMajor":1,"clientId":"render-study","commandId":"start-eye-1","action":"start","eventKinds":["eye-submitted"],"maxFrames":4,"maxDurationMs":2000,"maxEvents":8192,"maxResourceObservations":32768}
{"contractMajor":1,"clientId":"render-study","commandId":"stop-1","action":"stop","captureId":"capture-live-..."}
{"contractMajor":1,"clientId":"render-study","commandId":"events-1","action":"capture_events","captureId":"capture-live-...","offset":0,"limit":500}
```

`eventKinds` is optional. Omitting it preserves the original full event stream.
When supplied it must be a non-empty set of registry-advertised names. The
controller expands identity and paired-boundary dependencies and reports both
the requested and resolved sets; filtered calls are counted but are not drops,
gaps, or truncation. The eye-only example therefore also retains first-seen
resource observations needed to identify submitted textures.

The registry's `eventKinds` array is implementation-backed: every advertised
kind has a live recording path in the current build. Boundary and command-list
types retained as schema/collector scaffolding but not yet wired to a runtime
emitter are listed separately under `plannedEventKinds` and are rejected as
capture selectors until that emitter exists.

Capture-manifest 1.5 makes the selector semantics explicit under `bounds`:
`requestedEventKinds` records the caller's selector, `resolvedEventKinds`
records the dependency-expanded selector, and `observedEventKinds` records the
types actually written. The older non-empty `eventKinds` output inventory is
retained for 1.x compatibility; an empty artifact is represented there by the
synthetic `capture-marker` label, while `observedEventKinds` is correctly empty.

Capture-manifest 1.6 also freezes the resident shader-compilation context at
capture start under `extensions["csx.shaderCompilation"]`. It records the
actual global define text, compile flags, cache ABI, compiler identity and
global compile-state digest, plus the full external shader-compatibility
registration set needed to derive a family/source-specific requirement digest.
This closes the build-to-bytecode provenance gap without adding configuration
reads to render hooks. See
[`capture-provenance-contract.md`](./capture-provenance-contract.md) for the
qualification between global and shader-specific compile identity.

Capture-manifest 1.7 adds independent bounds and completion counters for the
scene-object, geometry, and material-state catalogues. Render-event 1.9 adds
their first-observed declarations and `geometry-boundary-v2`, which binds the
exact geometry observation and material-state revision used by a temporal
setup scope. The derived graph can therefore project a draw to observed scene,
geometry, and material identities without using pointer or name equality as a
join.

Render-event 1.10 extends each material-state declaration with up to 32 bounded
runtime texture bindings. A binding records its role, runtime material-list
position, bounded path, `NiSourceTexture` evidence, and exact capture-local
D3D11 resource observation. The index is explicitly not an HLSL register or
SRV slot. The `communityshaders.render_map` service contract 1.11 and schema
revision 12 introduced this addition; old 1.x material events remain valid
without the optional binding fields. Service contract 1.12 / schema revision
13 corrects the post-expansion default catalogue budget, defaults `maxBytes`
to the 32 MiB service ceiling, reports the exact admitted defaults and fixed
catalogue cost in `registry`, and returns actionable byte details when a custom
catalogue cannot leave room for events.

Render-event 1.11 and service contract 1.13 / schema revision 14 add
`draw-call-v3.preparedGeometrySetupObservationId`. This records the one-shot
same-thread, same-generation, same-immediate-context handoff from a selected
Skyrim `SetupGeometry` call to its following draw after the setup RAII scope has
ended. Every later geometry setup invalidates the candidate. Derived graph 1.8
uses a typed `prepares` edge and labels that association basis explicitly;
`scopes.geometry` remains null for the post-return draw.

Derived graph 1.9 / `static-semantic-resource-graph-6` resolves observed D3D11
views to exact mip/array subresource spans before applying the documented
read/write conflict rule. Buffers remain one D3D11 subresource. Unknown view
descriptor families fall back conservatively and are counted explicitly under
`csx.hazardOverlapFallbacks`; the model never silently upgrades an unknown
range to exact evidence.

Derived graph 1.10 / `static-semantic-resource-graph-7` consumes the effective
state returned by D3D11 after each hooked setter. Render-event 1.13 distinguishes
`requested-call`, `post-call-query`, and `capture-state-snapshot` resource-view
bindings; `resource-view-state-observed-v1` proves the queried slot range even
when no slot changed. The graph retains the exact-overlap calculation as a
prediction, counts agreement and disagreement with the queried state, and uses
the queried state—not the prediction—for subsequent draw/dispatch resource
edges. Texture-cube-array SRVs now retain their mip and face ranges as well.

Render-event 1.12 and service contract 1.14 / schema revision 15 add
`render-target-binding-v2.source`. If no output-merger bind has been observed in
the active capture, the first immediate-context draw queries its effective
render targets on the render thread and emits one `capture-state-snapshot`.
An observed OM bind remains `observed-call` and suppresses the snapshot. This
closes capture-start state gaps without continuously retaining pre-capture COM
objects or mislabelling queried state as a bind call.

Service contract 1.15 / schema revision 16 adds post-call effective-state
observation for all immediate-context SRV stages plus compute/output-merger UAVs.
Output-merger setters query the resulting targets and scan every SRV stage;
shader-resource setters query the affected range. A one-shot first-draw scan
seeds state when capture begins between setters. Delta-encoded binding events
keep event volume bounded, while a summary event makes an unchanged query
explicit rather than indistinguishable from missing evidence.

Service contract 1.16 / schema revision 17 raises the opt-in capture-storage
ceiling and admitted default from 32 MiB to 64 MiB. Effective-state events made
the former 32 MiB ceiling too small for the default observation catalogues plus
even one immediate loaded-scene execution slice. Capture remains disabled by
default, and every active capture is still bounded by the same caller-visible
byte limit; the larger ceiling makes the advertised default profile useful
without phase-sensitive catalogue tuning.

Render-event 1.14 and service contract 1.17 / schema revision 18 add bounded
immediate-context `Map`/`Unmap` observation. Successful readable Map return is
recorded as the CPU-visibility boundary; a matched writable Unmap is the D3D11
publication boundary. Failed nonblocking maps remain failed observations and do
not become graph reads. Graph 1.11 pairs map lifetimes, versions writable
unmaps, and retains measured Map-call QPC duration without copying mapped data.
The exact semantics, limits, and pending live qualification are documented in
[`cpu-resource-access-boundary-slice.md`](./cpu-resource-access-boundary-slice.md).

The zero-loss capture that exposed this boundary, the corrective loaded-scene
qualification, the subsequent output-merger capture-start qualification, their
retained hashes, and exact teardown evidence are recorded in
[`material-input-execution-live-capture-2026-08-29.md`](./material-input-execution-live-capture-2026-08-29.md).

Validate the schemas, examples, prior-art provenance and target joins, stable
shader/engine references, event ordering, causal references, observation
scopes, and derived graph references from the repository root:

```powershell
pwsh -NoProfile -File tools/test-render-map-contracts.ps1
```

Derive an execution/resource graph from a completed live capture. Supplying the
static manifests is optional, but enables the stronger join: exact runtime
loader-family aliases resolve to deterministic shader compile units, and an
exact bound vertex/pixel descriptor pair resolves to a versioned engine-map
technique when there is one unique match. Multiple matches become an explicit
ambiguity; the builder never selects one by naming similarity.

```powershell
python tools/build-render-graph.py `
  --capture-manifest <capture>/capture-manifest.json `
  --events <capture>/events.jsonl `
  --shader-manifest docs/development/shader-analysis/shader-manifest.generated.json `
  --engine-map docs/development/render-map/engine-map.skyrim-vr-1.4.15.main-menu-seed.json `
  --output <capture>/render-graph.json
```

The first live resource-flow graph and its exact retained hashes are documented
in
[`resource-flow-main-menu-capture-2026-08-26.md`](resource-flow-main-menu-capture-2026-08-26.md).

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

Public prior art is intentionally one layer removed from these joins. A claim
records the source's Skyrim runtime and shader-system revision independently
from the target. It may reference candidate target entities, but it becomes an
engine-map fact only after a target-specific spot check and separate review.

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
