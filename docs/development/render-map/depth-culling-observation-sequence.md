# VR depth-culling observation sequence

## Purpose

This is the narrow integration contract between the current-frame VR
depth-culling diagnostics and the general render-map stream. It records what is
actually exposed today, what each observation proves, and what the next live
capture must establish. It does not turn pointer proximity into semantic
identity.

## Exposed native sequence

The current Skyrim VR 1.4.15 hooks expose this order:

1. `CurrentFrameDepthCullingAccumulate` wraps native RVA `0xDA1860` for each
   `NiAVObject`. The post-native hook resolves the newly assigned result-array
   index and emits `visibility-candidate`.
2. `Main_RenderDepth` is hooked through relocation `(100421, 107139)`. Its VR
   depth-downscale call is at caller offset `0x37F`, followed by the OBB shader
   call at `0x3B1`.
3. `CurrentFrameDepthCullingObbRender` wraps the OBB shader. After the native
   call returns, `visibility-result-ready` identifies the GPU result SRV and a
   new resource version.
4. Lighting, distant-tree, and grass geometry setup resolve the object index,
   bind the selected visibility SRV at VS slot 127, query the effective binding
   with `VSGetShaderResources`, and emit `visibility-consumed`.
5. The consumer event allocates a submission observation. The next actual
   immediate-context `Draw*` on the same capture generation, thread, and context
   consumes that observation. Later draws do not inherit it.
6. An accepted `IVRCompositor::Submit` emits `eye-submitted` for the submitted
   D3D11 texture, exact OpenVR eye, texture bounds, flags, and compositor cycle.
7. When the existing bounded diagnostic staging readback completes, every
   covered object emits `cull-decision` with the same resource-version ID,
   object index, producer result, producer frame, and per-category draw counts.
   Its readiness domain is `cpu-readback-complete`; it is analysis evidence,
   not a prerequisite for the live GPU consumer.

The graph builder now joins a visibility-associated draw to accepted eye
submissions only through its observed output content version. A direct route
requires the exact output allocation to remain the current submitted version.
An indirect route must traverse monotonically ordered `Copy*`/`Resolve*`
resource-flow events. Later clear, CPU update, or full-copy writes sever an old
route. Later draw or dispatch writes may carry prior contents forward, but that
edge is deliberately `correlated` at medium confidence because exact pixel
survival and blend coverage are not yet observed.

When the same route reaches left and right submissions in one CPU frame, the
decision window records both-eye coverage and distinguishes a shared stereo
texture with different bounds from distinct eye resources. Multiple valid
routes for one eye become explicit ambiguity groups; the builder does not pick
one by proximity.

The relocation and member offsets above are runtime evidence points, not yet
complete semantic names for every native type. The result-buffer pointer is
read from culler offset `0x100`; its SRV is read from the wrapper at `+0x8`.
Candidate count is read at culler offset `0xB0`. Object result and producer
frame fields are at `NiAVObject +0x128` and `+0x130`.

## Resource and readiness identity

The visibility result uses the general identity:

```text
resource observation + subresource range + write epoch
```

The initial implementation versions the whole structured result buffer as
subresource range `[0, 1)`. Its readiness domain is
`same-immediate-context-order`: the OBB producer returned before the later SRV
consumer was submitted on the same immediate context. This proves GPU ordering
and consumability by that later command. It does not claim CPU completion or
perform a synchronous readback.

D3D11 can null a conflicting binding. For the visibility slot, the stream
therefore records both requested and effective SRV observations plus
`bindingMatches`. General pipeline hazard reconstruction remains deferred.

## Submission identity

Geometry and technique setup scopes frequently close before the actual draw.
The visibility consumer therefore creates an explicit submission observation
containing the render-pass pointer, geometry pointer, object index, result
resource version, requested/effective views, draw category, slot, and control
mode. `Draw*` carries that observation in a dedicated envelope field. A new
declaration replaces an unconsumed declaration; capture generation and context
must match before consumption.

The forced-visible control does not disable candidate collection or the OBB
producer. It substitutes the all-visible SRV only at the final consumer and
records `forcedVisible: true`, preserving producer and submission topology for
a structurally comparable pair of captures.

## Eye attribution

Eye attribution begins at an evidence-bearing boundary: successful OpenVR
submission. Each accepted submit identifies the actual D3D11 texture resource,
left or right eye, and source bounds. This supports both common layouts:

- two distinct per-eye textures; or
- one shared stereo texture submitted twice with different bounds.

It deliberately does not back-label preceding draws as left or right. The
resource-flow graph must connect a draw's target through observed copy/resolve
edges to the eye-submitted texture. If one instanced or stereo draw contributes
to both accepted submissions, the derived report may label it `eye: both` only
after that resource path is proven.

## Refined first target

The first decision-window capture should select one persistent, ordinary
`BSLightingShader` geometry that is opaque, non-alpha-tested, non-blended,
non-skinned, and non-instanced. Grass, distant trees, foliage, particles, water,
and effect geometry remain observed but are not acceptance evidence for the
first slice.

The shortest remaining path is:

1. capture the native candidate and exact result-buffer version;
2. prove one explicit candidate/submission/draw association;
3. confirm requested and effective VS slot 127 are identical;
4. validate the derived draw-content-version route to one or both accepted eye
   submissions;
5. validate the completed diagnostic readback decision joined to the same
   object index and version;
6. emit the decision-window report for live and forced-visible controls.

Steps 4 and 6 are implemented in the offline join contract. They remain live
acceptance gates until a bounded in-world capture supplies both-eye routes with
zero dropped events.

Predication is not assumed: `SetPredication` requires an actual
`ID3D11Predicate`, not the existing arbitrary visibility buffer. Indirect
drawing is also not assumed because it would require replacing the direct draw
and a resource created with `D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS`.
