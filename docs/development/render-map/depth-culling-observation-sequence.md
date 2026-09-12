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
   call returns, it validates and owns an immutable token containing the exact
   structured SRV/resource, view range, culler, CPU arrays, object count, CSX
   CPU frame, and engine depth generation. `visibility-result-ready` names that
   token's new resource version. Every producer attempt clears the older token
   first.
4. Lighting, distant-tree, and grass geometry setup resolve the object index
   against that token and stage a submission tied to the active render pass,
   geometry, producer serial, and object index. Setup does not bind slot 127.
5. After dirty-state upload, the matching submission is armed. Immediately
   before the actual immediate-context `Draw*`, a scoped draw guard saves VS
   slot 127, binds the token SRV, reads the effective binding back, and emits
   `visibility-consumed` only when the exact view is effective. The same draw
   consumes that explicit observation. On every exit the guard restores the
   previous slot, disables the descriptor, and clears the submission; later or
   unrelated draws cannot inherit it.
6. An accepted `IVRCompositor::Submit` emits `eye-submitted` for the submitted
   D3D11 texture, exact OpenVR eye, texture bounds, flags, and compositor cycle.
7. When the existing bounded diagnostic staging readback completes, every
   covered object emits `cull-decision` with the same resource-version ID,
   object index, producer result, producer frame, and per-category draw counts.
   Its readiness domain is `cpu-readback-complete`; it is analysis evidence,
   not a prerequisite for the live GPU consumer.

The relocation and member offsets above are runtime evidence points, not yet
complete semantic names for every native type. The result-buffer pointer is
read from culler offset `0x100`; its SRV is read from the wrapper at `+0x8`.
Candidate count is read at culler offset `0xB0`. Object result and engine-frame
fields are at `NiAVObject +0x128` and `+0x130`. Candidate and result events
carry both `producerFrame` (the CSX CPU frame) and `engineFrame` (the native
depth-culling generation); the join requires both.

## Resource and readiness identity

The visibility result uses the general identity:

```text
resource observation + subresource range + write epoch
```

The implementation versions the whole structured result buffer as subresource
range `[0, 1)` and separately records the buffer-view element range. Its
readiness domain is
`same-immediate-context-order`: the OBB producer returned before the later SRV
consumer was submitted on the same immediate context. This proves GPU ordering
and consumability by that later command. It does not claim CPU completion or
perform a synchronous readback.

D3D11 can null a conflicting binding. For the visibility slot, the stream
therefore records both requested and effective SRV observations plus
`bindingMatches` at the final draw boundary. A viable graph additionally
requires the draw-time vertex slot state, ready/requested/effective view,
version resource, and object/count range all to agree. General pipeline hazard
reconstruction remains deferred.

## Submission identity

Geometry and technique setup scopes frequently close before the actual draw.
The staged draw token therefore carries render-pass pointer, geometry pointer,
object index, producer serial, result resource version, draw category, slot,
and control mode through dirty-state upload. Only its exact scoped `Draw*`
creates and consumes the render-map submission observation. Capture generation,
context, pass, producer, and final effective binding must all match.

The forced-visible control does not disable candidate collection or the OBB
producer. It substitutes a validated structured all-visible SRV only at the
final consumer and records `forcedVisible: true`, preserving producer and
submission topology for a structurally comparable pair of captures. Because
that control deliberately reads a different resource, it is control evidence,
not proof that the live producer resource was consumed.

## Eye attribution

Eye attribution begins at an evidence-bearing boundary: successful OpenVR
submission. Each accepted submit identifies the actual D3D11 texture resource,
left or right eye, and source bounds. This supports both common layouts:

-   two distinct per-eye textures; or
-   one shared stereo texture submitted twice with different bounds.

It deliberately does not back-label preceding draws as left or right. The
resource-flow graph must connect a draw's target through observed copy/resolve
edges to the eye-submitted texture. If one instanced or stereo draw contributes
to both accepted submissions, the derived report may label it `eye: both` only
after that resource path is proven.

The guarded Bleakwind capture adds a second, direct draw-level attribution
route for VR Lighting. All 1,691 visibility-consumer submissions in its complete
frame join to `DrawIndexedInstanced` with `InstanceCount = 2`. The bound shader
accepts `SV_InstanceID`; `Stereo::GetEyeIndexVS(instanceID)` maps instance 0 to
left and instance 1 to right when VR stereo is enabled. These draws may
therefore be labelled `eye: both` from their proven execution mechanism even
though the later draw-target-to-OpenVR copy/resolve chain remains incomplete.
The capture must not use the later compositor submission alone to back-label an
otherwise unidentified draw.

## Refined first target

The first decision-window capture should select one persistent, ordinary
`BSLightingShader` geometry that is opaque, non-alpha-tested, non-blended,
non-skinned, and non-instanced. Grass, distant trees, foliage, particles, water,
and effect geometry remain observed but are not acceptance evidence for the
first slice.

The shortest remaining path is:

1. trace the draw target to one or both accepted eye submissions;
2. add the selected object's scene/material/pass identity;
3. validate the completed diagnostic readback decision joined to the same
   object index and version;
4. emit the decision-window report for live and forced-visible controls.

Predication is not assumed: `SetPredication` requires an actual
`ID3D11Predicate`, not the existing arbitrary visibility buffer. Indirect
drawing is also not assumed because it would require replacing the direct draw
and a resource created with `D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS`.
