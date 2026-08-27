# Existing render and shader evidence synthesis — 2026-08-24

This report analyses the retained render-map captures, the current static shader
manifest, shader-cache source, the depth-culling measurements, and the earlier
shader-residency baselines. It does not add a new runtime capture.

> **Status re-baseline — 2026-08-26:** this document preserves the conclusions
> available from the 24 August evidence set. Subsequent typed-observation,
> immediate-context, output-merger, and resource-flow slices closed many of the
> identity gaps listed below. The current implementation now joins typed
> resources and RTV/DSV/SRV/UAV views to ordered immediate-context draws,
> dispatches, copy/resolve operations, selected shaders, and output-merger
> bindings. See
> [`resource-flow-graph-slice.md`](./resource-flow-graph-slice.md) and
> [`resource-flow-main-menu-capture-2026-08-26.md`](./resource-flow-main-menu-capture-2026-08-26.md).
> The remaining graph-correctness gap is no longer basic target/draw identity;
> it is resource write-version identity, effective D3D11 hazard state,
> subresource precision, deferred contexts, eye attribution, and broader
> mutation-operation coverage.

The first concrete Skyrim VR engine-map seed produced from this analysis is
[`engine-map.skyrim-vr-1.4.15.main-menu-seed.json`](./engine-map.skyrim-vr-1.4.15.main-menu-seed.json).

## Executive conclusion

The evidence now joins the main-menu runtime stream to CSX's static shader map
at the engine-family and descriptor-key level:

```text
Skyrim shader family
  -> SetupTechnique implementation
  -> BSShader::BeginTechnique
  -> vertex/pixel descriptor lookup keys
  -> CSX engine-cache family and HLSL source
  -> candidate cache path
```

That is a material improvement over a family-only map. The remaining missing
identity is narrower and precise: the capture did not preserve the concrete
ImageSpace shader name, global define snapshot, resolved cache path, or the
selected shader object/bytecode hashes. It also did not preserve D3D command,
target, draw, eye, or submission identity. Consequently, it can identify the
candidate compiled shader family and descriptor, but cannot yet prove the exact
cache file or draw that consumed it.

The depth-culling evidence independently establishes a stronger architectural
conclusion. Current-frame visibility fixed the temporal correctness problem,
but a per-vertex early return after draw submission eliminated essentially no
measured GPU work in the tested static interior. Useful culling must move the
decision before vertex work—CPU draw suppression, zero-instance submission,
predication, or an indirect-command path—not merely branch inside the submitted
vertex shader.

## Inputs and compatibility limits

| Evidence | What it can prove | Important limit |
|---|---|---|
| 120-frame main-menu capture `capture-live-00008b13098138bc-1` | Runtime order, nesting, types, descriptors, pass values, and caller RVAs | No typed observations, D3D identity, eye, targets, or shader/cache provenance |
| Generated shader manifest SHA-256 `0b87bc...b6e6f` | Source closure, compile route, feature ownership, stable compile/pass IDs | Does not observe the runtime object selected by Skyrim |
| Skyrim VR PDB plus runtime-decrypted 1.4.15 image SHA-256 `1e08d1...a4e8fe` | Containing functions, vtable targets, and `BeginTechnique` behaviour | Static disk `SkyrimVR.exe` is Steam-protected; its on-disk code is not suitable for this disassembly |
| Depth-culling telemetry and GPU timestamp experiment | Visibility production/consumption and bounded post-occlusion GPU timing | Different build/scenario from the main-menu render capture; the event streams cannot be merged |
| 2026-08-20 shader residency/profiler baseline | Cache population differences and coarse enabled/inactive/unloaded timing | Route and resolution changed between states; values are not additive per-feature costs |

Cross-run conclusions below use only relationships that remain valid despite
those provenance differences. No event from one run is represented as though it
occurred in another.

## New facts derived from the raw 120-frame stream

### One exact semantic frame signature

All 120 frames have the same normalized event sequence, not merely the same
counts. Ignoring QPC values, pointers, capture-local scope numbers, and frame
numbers, every event's type and semantic payload repeats exactly.

Each frame contains:

- 7 balanced render-pass scopes;
- 22 balanced technique scopes;
- 4 balanced geometry scopes;
- 4 shader families;
- the same 7 pass values in the same order; and
- the same 8 descriptor combinations.

There are 7 techniques inside render-pass scopes and 15 outside them. The
unscoped set is 14 ImageSpace calls and one Utility call per frame. This is a
deterministic main-menu signature and a useful future regression baseline. More
frames of the same six event kinds and scenario would add sample volume but no
new graph edge.

The CPU-frame start deltas have a 13.4673 ms median and 14.2433 ms mean
(approximately 70.2 starts per second), with 9.2102 ms minimum, 24.1184 ms p95,
and 28.4403 ms maximum. These are CPU QPC intervals, not GPU frame times.

### Exact per-frame order and associations

| Order | Pass value | Flags | Alpha | Selected family and descriptors |
|---:|---:|---:|:---:|---|
| 1 | 81,989 | 1,024 | no | Utility `0x0001401A / 0x00014002` |
| 2 | 8,261 | 33 | no | Utility `0x00002000 / 0x00002000`, skip pixel shader |
| 3 | 536,879,147 | 33 | no | Utility `0x00002000 / 0x00002000`, skip pixel shader |
| — | no pass scope | — | — | Utility `0x01002002 / 0x01062002` |
| 4 | 1,140,850,733 | 513 | no | Effect `0x04000001 / 0x04000001` |
| 5 | 1,224,753,773 | 513 | no | Lighting `0x01000200 / 0x01004201` |
| 6 | 1,074,270,451 | 517 | yes | Effect `0x000010C7 / 0x000810C7` |
| — | no pass scope | — | — | 13 ImageSpace `0 / 0` calls across three caller functions |
| 7 | 1,073,741,934 | 516 | yes | Effect `0x00000042 / 0x00000042` |
| — | no pass scope | — | — | final ImageSpace `0 / 0` call |

The scope spans are CPU call-boundary timings only. The median observed pass
spans were 50.5 microseconds for the Lighting pass; 10.6, 37.1, and 17.2
microseconds for the three Effect passes; and 26.6, 7.9, and 3.2 microseconds
for the three Utility passes. They are useful for detecting hook-order or gross
behaviour changes, but they do not measure GPU shader cost.

## Static reverse-engineering result

All six captured caller RVAs are return addresses immediately following calls
to `BSShader::BeginTechnique` at Skyrim VR RVA `0x1365A70`:

| Captured return RVA | Containing function |
|---:|---|
| `0x1337D7B` | `BSLightingShader::SetupTechnique` at `0x1337D00` |
| `0x13411CB` | Effect-family slot-2 candidate at `0x1341190` |
| `0x134FAC1` | `BSImagespaceShader` slot-2 function at `0x134FAB0` |
| `0x1351AD4` | `BSUtilityShader::SetupTechnique` at `0x1351A50` |
| `0x13754E8` | unresolved ImageSpace helper at `0x13754C0` |
| `0x13758F8` | unresolved ImageSpace helper at `0x13758D0` |

The PDB explicitly names `BSLightingShader::SetupTechnique` and
`BSShader::BeginTechnique`. CommonLib vtable evidence proves slot 2 for
Lighting, ImageSpace, and Utility; the Effect identity remains a high-confidence
family/role correlation because its public symbol is unnamed.

`BSShader::BeginTechnique` uses the supplied vertex and pixel descriptors as
keys into the shader object's two lookup structures. If both records are found,
it binds the selected vertex shader and—unless requested to skip it—the selected
pixel shader. The capture therefore records the exact engine lookup keys, not a
post-hoc classification.

## Descriptor decoding and static-manifest join

The generated manifest currently supplies these family joins (the generated
ordinal IDs must be refreshed when the shader inventory changes):

| Runtime family | Static compile unit | Static pass | Source |
|---|---|---|---|
| Lighting | `compile-0115` | `pass-0115` | `Lighting.hlsl` |
| Effect | `compile-0074` | `pass-0074` | `Effect.hlsl` |
| Utility | `compile-0120` | `pass-0120` | `Utility.hlsl` |
| ImageSpace | 40 candidate engine-cache families | unresolved | `IS*.hlsl` family selected by runtime name |

The captured keys decode as follows:

- Lighting `0x01000200 / 0x01004201` is technique 1, `Envmap`. The vertex key
  carries `Specular`; the pixel key carries `VC`, `Specular`, and `DefShadow`.
- Effect `0x000010C7 / 0x000810C7` is a textured, indexed particle path; the
  pixel key additionally carries `GrayscaleToColor`.
- Effect `0x00000042 / 0x00000042` carries `TexCoord` and `Texture`.
- Effect `0x04000001 / 0x04000001` carries `VC` and
  `MotionVectorsNormals`.
- Utility `0x0001401A / 0x00014002` expands to the shadow-map/PB path; the
  vertex key additionally requests normals.
- Utility `0x00002000 / 0x00002000` expands to `RENDER_DEPTH` and
  `NO_PIXEL_SHADER`, matching the captured skip-pixel request.
- Utility `0x01002002 / 0x01062002` expands to the depth plus
  `RENDER_SHADOWMASKDPB` path, with shadow filters 0 and 3 respectively.

CSX's disk path is constructed from shader name, descriptor, stage, and a
suffix derived from the canonical global define set. For the non-ImageSpace
families this narrows the candidates to paths such as
`Data/ShaderCache/Lighting/1000200<suffix>.vso` and
`Data/ShaderCache/Lighting/1004201<suffix>.pso` (and equivalent Effect/Utility
paths). It does not prove the suffix or file contents because the capture did
not receive the define snapshot or cache inventory. ImageSpace remains broader:
the engine supplied `0 / 0`, while CSX resolves a different cache descriptor
from the concrete `BSImagespaceShader::name`; that name was not captured.

## What the depth-culling data actually says

The current-frame implementation fixed the observed temporal curtain flash.
In the fixed Breezehome test it also showed that the visibility data was active
and consumed:

- approximately 57.8% of candidate objects were classified occluded;
- approximately 57.2% of covered Lighting draws were classified occluded; and
- the current-frame visibility buffer was bound on about 740 draws per frame.

Despite that high classification rate, 6,694 valid GPU timestamp samples showed
24.198978 ms forced-visible versus 24.198682 ms live visibility: a 0.000296 ms,
0.0012% difference. The two sandwich directions disagreed in sign.

This is consistent with the implementation point. Draws were already submitted,
input assembly and vertex invocation remained, and the culling test performed a
per-vertex early return. Hidden pixels were also likely already cheap under
ordinary depth rejection. The experiment does not show that occlusion
information is useless; it shows that consuming it at that shader location is
too late to eliminate the expensive work.

The static interior did not exercise the strongest possible vertex-heavy cases
(large skinned meshes, grass, or distant trees), so the result should not be
generalized into an absolute zero. It is nevertheless strong evidence against
continuing to optimize the present per-vertex suppression mechanism as the main
culling route.

## Shader residency evidence in render-map terms

The 2026-08-20 cache comparison found 3,615 enabled files versus 3,606
startup-unloaded files. Among common paths, 2,131 were byte-identical and 1,475
changed; only 9 paths existed solely in the enabled tree. The changed paths were
concentrated in Lighting (710), Effect (416), and Water (344).

This matters for the render map in two ways:

1. Feature state often changes the bytecode behind the same family/descriptor
   path rather than adding or removing a whole shader family.
2. A descriptor alone is therefore insufficient artifact identity. The global
   define snapshot, source closure digest, and compiled bytecode hash belong in
   the join.

The coarse profiler results cannot be interpreted as direct shader-family
savings. Resident-inactive was slightly slower than enabled in Breezehome and
slightly faster in Riften; startup-unloaded was much lower, but also changed
routes and resolution. Upscaling dominated the named Riften timers. The safe
conclusion is about residency and identity, not additive per-feature cost.

## Proven, correlated, and still missing

### Proven

- The main-menu event grammar is deterministic across the retained 120 frames.
- The six caller RVAs invoke `BSShader::BeginTechnique`.
- The captured descriptors are the actual engine lookup keys used before shader
  binding.
- Lighting, Effect, and Utility keys join to stable CSX compile/pass families.
- Current-frame visibility fixed the tested temporal correctness issue.
- Per-vertex suppression produced no measurable GPU saving in the fixed static
  interior test.

### Correlated, not fully proved

- RVA `0x1341190` is the Effect-family slot-2 SetupTechnique implementation.
- The candidate cache filenames for Lighting, Effect, and Utility follow from
  family, stage, and descriptor, but their define suffix and contents were not
  captured.

### Missing hard joins

The following list is the original 24 August gate. Items marked **closed** were
subsequently implemented and live-validated; the unmarked items remain current.

- concrete ImageSpace shader name and CSX-resolved descriptor;
- global feature/define snapshot and source-closure digest;
- selected vertex/pixel wrapper, D3D object, and bytecode hash;
- **closed:** typed observation declaration and capture-generation identity;
- **closed for the immediate context:** device context and monotonic command
  sequence;
- **closed for immediate-context application calls:** render/depth target
  identity and draw/dispatch identity;
- resource write epochs and producer-version identity;
- effective state after D3D11 automatic hazard unbinding, including exact
  subresource overlap;
- deferred-context command-list identity, eye, and final submission;
- a same-build gameplay capture containing both render-map and depth-culling
  events.

## Highest-value next slice

The original recommended identity sequence was completed through bounded
immediate-context resource flow. The current highest-value generic slice is to
make that graph causally honest before increasing gameplay capture volume:

1. Derive allocation-wide resource write versions so every read consumes one
   explicit content epoch rather than a timeless allocation node.
2. Derive conservative RAW, WAR, and WAW execution dependencies and distinguish
   them from directly observed API calls.
3. Reconstruct conservative same-allocation D3D11 automatic SRV/UAV/output
   hazard unbinding, while explicitly retaining the lack of exact subresource
   overlap proof.
4. Observe the remaining common mutation routes: clears, updates, structure
   count copies, and mip generation.
5. Then project pass, technique, geometry, and selected-shader identities onto
   the versioned execution graph and perform controlled gameplay captures.

That sequence avoids producing a visually convincing but causally false frame
graph. The original object-to-bytecode-to-draw objective remains, but its
resource edges will identify the content version consumed and the confidence of
each derived ordering constraint.
