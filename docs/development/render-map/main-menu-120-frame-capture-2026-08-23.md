# Main-menu 120-frame render capture — 2026-08-23

This report incorporates the depth-culling implementation review of capture
`capture-live-00008b13098138bc-1`. The run closes the bounded-capture and durable
finalization milestone. It does **not** yet establish D3D11 draw identity, stereo
coverage, or a viable depth-culling decision window.

## Provenance

- source commit: `768ed88ee5332a26972376e4bd29457cbc5d89a8`
- CSX build ID:
  `42025c7a7c50d842a48ce4175a208775e2686fbdee19e839caca73a6b7709a59`
- diagnostic `CommunityShaders.dll` SHA-256:
  `b6bf4b1d9e130aa1c61734015ab4b135d823500d85c014bda03985947815b508`
- event stream SHA-256:
  `7e0521c344b56b8b47a34ea639466f1ca72dc984162e9e19764c81c869da1a5e`
- capture manifest SHA-256:
  `a75dfaa997b5c5bb2c1db5b831b6918043c62cb033fabaca913e97c1f8f13bd3`
- derived capture summary SHA-256:
  `b6f1d1838d09adff2dd56497c679caafa216c279df64cc093e10d70e849c1a0e`
- cleanup receipt SHA-256:
  `754168fa06e431e9656ddf3e402767a3d695feb4e057e2dfd3683a1128ce031c`

Raw evidence is retained outside the repository beneath
`L:/Codex/evidence/render-map-live-capture/20260823T232300Z-opaque-depth-culling`.

The scenario was the Skyrim VR main menu under the Valve null-HMD route. The
in-process manifest did not receive that verified orchestration metadata, so
its `scenario` and `runtimeRoute` fields remain explicitly unspecified/unknown.

## Capture integrity

The manifest and all 7,920 event records validate against their current JSON
schemas. Sequences are contiguous from 0 through 7919. The recomputed event
hash matches the manifest. There were no dropped events, truncation, scope
mismatches, scope overflows, or stop-race rejections.

Capture stopped at exactly 120 CPU frames. Only one hook call reached the
inactive boundary after the frame limit, confirming that the finalization fix
removed the earlier 31,656 post-bound rejection tail.

Cleanup evidence reports Skyrim, MO2, and SteamVR closed, the temporary task
profile and mod removed, MO2 access released, null HMD disabled, and SteamVR
settings restored.

## Observed workload

The main-menu workload was regular across the sample:

- 120 CPU frames and exactly 66 events per frame;
- one observed rendering thread;
- 1.6960182 seconds between the first and last event QPC timestamps;
- 840 render-pass scopes;
- 2,640 technique scopes; and
- 480 geometry-setup scopes.

Every begin/end pair was balanced.

CommonLib's `RE::BSShader::Type` establishes the shader-family decoding:

| Numeric type | Shader family | Technique begins |
|---:|---|---:|
| 5 | ImageSpace | 1,680 |
| 6 | Lighting | 120 |
| 7 | Effect | 360 |
| 8 | Utility | 480 |

Eight exact `(type, vertex descriptor, pixel descriptor, skip pixel)`
combinations were observed. They are runtime observations, not yet semantic
technique names:

| Type | Vertex descriptor | Pixel descriptor | Skip PS | Count |
|---:|---:|---:|:---:|---:|
| 5 | 0 | 0 | no | 1,680 |
| 6 | 16,777,728 | 16,794,113 | no | 120 |
| 7 | 4,295 | 528,583 | no | 120 |
| 7 | 66 | 66 | no | 120 |
| 7 | 67,108,865 | 67,108,865 | no | 120 |
| 8 | 16,785,410 | 17,178,626 | no | 120 |
| 8 | 8,192 | 8,192 | yes | 240 |
| 8 | 81,946 | 81,922 | no | 120 |

Seven render-pass values were each observed 120 times: `8261`, `81989`,
`536879147`, `1073741934`, `1074270451`, `1140850733`, and `1224753773`.
They remain numeric observations until engine or shader evidence establishes
their meaning.

The technique callsites form a useful first static-analysis queue:

| Skyrim VR caller RVA | Shader type | Begins |
|---:|---:|---:|
| `0x1337D7B` | 6 | 120 |
| `0x13411CB` | 7 | 360 |
| `0x134FAC1` | 5 | 1,440 |
| `0x1351AD4` | 8 | 480 |
| `0x13754E8` | 5 | 120 |
| `0x13758F8` | 5 | 120 |

`0x1351AD4` already has tracked CSX source evidence as one allowlisted Utility
shadowmask caller for Terrain Blending's slot-2 and depth override. That does
not yet name its containing Skyrim function or prove the semantics of every
captured invocation.

## Pointer identity result

Raw pointer equality is disproven as a semantic identity rule in this sample.
For example, render-pass pointers `0x16FA1047C90` and `0x16FA1047F60` each
participated in both pass `8261` and pass `81989`. Conversely, pass
`536879147` rotated through seven pointers. This is compatible with mutation,
pooling, replacement, or a combination; the capture does not distinguish
those lifetimes.

Pointers remain useful evidence inside one process, but every live object must
receive a capture-local, kind-specific identity and generation. A pointer that
is reused after a proved or conservatively inferred lifetime boundary must
receive a new generation.

## Semantic-join failure

The stream is structurally valid but not yet eligible for a render-graph join.
All events still have unknown eye attribution, null scene/submission epochs,
null device-context and command-stream identity, no GPU timestamps, and empty
manifest, engine, observation, and causal references.

Scope values such as `obs-render-pass-1-g1` are serialized, but no typed
`observationRef` declares the ID, kind, role, pointer evidence, or generation
meaning. The example semantic validator correctly requires scope IDs to refer
to known observations; applying that rule to this live stream would reject it.

This is the next hard gate. Adding draw events before fixing it would create a
larger stream with precise-looking but unresolved scope references.

## Incorporated next-step plan

### Phase A — use the existing capture without another runtime run

1. Create the first non-example Skyrim VR 1.4.15 engine-map seed.
2. Add source/CommonLib evidence for shader types 5–8.
3. Inspect the six caller RVAs and identify containing functions, callsites,
   and render phases where static evidence permits.
4. Record the seven pass values and eight descriptor combinations as observed
   entities or unresolved domains; do not invent technique names.
5. Record `0x1351AD4`'s existing Terrain Blending relationship separately from
   any still-unproved engine-function identity.

### Phase B — make current runtime scopes joinable

1. Add a capture-local observation registry for render pass, technique,
   geometry, and shader.
2. Emit the typed observation declaration before, or on, its first reference.
3. Track pointer generations conservatively and prevent silent ID reassignment.
4. Extend semantic validation to check declaration order, scope kind, balanced
   ownership, generation reuse, device-context references, and exact capture
   membership against an actual events file rather than examples only.
5. Make capture start accept verified provenance supplied by the controller:
   build and manifest paths/hashes, executable and DLL hashes, profile/load
   order identities, runtime route, shader cache identity, and scenario ID.

### Phase C — add the first bounded D3D11 command slice

1. Assign an observation ID to the immediate device context.
2. Maintain a monotonic command-stream sequence per context.
3. Observe `Draw`, `DrawIndexed`, instanced/indirect draw variants, and
   `Dispatch`/`DispatchIndirect` behind explicit capture categories.
4. Observe the narrow render/depth-target binding boundary needed to attach
   active target IDs.
5. Incrementally track bound shaders and essential pipeline state, then attach
   an immutable pipeline-state observation to each draw.
6. Preserve unknown eye coverage until an engine or target/submission boundary
   proves left, right, both, or mono.

### Phase D — next runtime sequence

1. Short main-menu proof that typed observations, D3D context/draw events,
   target identity, eye attribution, and semantic validation are safe.
2. Fixed gameplay capture of one isolated opaque `BSLightingShader` object.
3. Add visibility candidate, result-ready, view-validity, and consumption
   boundaries from the depth-culling implementation.
4. Run the Breezehome curtain scenario to validate the resulting decision
   window behaviourally.

No further capture using only the present six event kinds is warranted; it
would add sample volume without closing a graph edge.
