# VR Current-Frame Depth Culling

## Required behaviour

Occlusion must be decided from the complete scene and the depth produced for
the current stereo view. An object may be skipped from expensive later work
only when the current OBB result says that neither eye can see it. Camera-motion
thresholds, edge-of-view tests, and other attempts to make a previous frame's
answer safe are not part of this design.

Skyrim's original delayed implementation remains the fallback. Disabling the
experimental current-frame option must leave the native depth-culling setting,
OBB collection, GPU test, readback, and `CopyTransformAndBounds` correction in
place.

## History

[PR #1858](https://github.com/community-shaders/skyrim-community-shaders/pull/1858)
did not first create the CSX depth-culling integration. It repaired and enabled
the existing Skyrim VR OBB path when upscaling or Terrain Blending was active:
it removed forced-disable cases, supplied conservative per-eye depth upscaling,
and bound the correct depth SRV for OBB testing.

The earlier CSX introduction is commit `534f60ce3` from
[PR #1075](https://github.com/community-shaders/skyrim-community-shaders/pull/1075).
That change exposed the engine settings and corrected the model-bound
translation copied by `BSGeometry::CopyTransformAndBounds`; it did not add
same-frame GPU consumption.

The separately tagged `moc-baseline` line is Nukem's CPU masked-occlusion
implementation for Skyrim SE 1.5.97. It is useful reference material, but it is
not a lost Skyrim VR current-frame GPU implementation and is not an ancestor of
the current `main-VR` line.

## Verified Skyrim VR engine flow

The observations below come from Ghidra analysis of reconstructed Skyrim VR
1.4.15 image SHA-256
`74888F09EC3675B92D5CA685BF914BC60571B5E56BB4EEFDAE634C5B0C0121E5`.
Addresses are RVAs from the executable image base.

-   `0xDA1860` is the per-object accumulation function. It reads the pointer at
    `NiAVObject+0x128`, accepts it only when the frame stamp at `+0x130` is one
    engine frame old, and can suppress the object before assigning its new OBB
    slot. This proves that native CPU culling consumes a previous-frame result.
-   The culler pointer is stored at `0x36F1870`; the engine frame counter is at
    `0x3186C5C`.
-   The culler holds at most 4096 OBBs. Its object count is at `+0xB0`, its two
    CPU result-array pointers are at `+0xD0` and `+0xD8`, and its GPU result
    buffer wrapper is at `+0x100`.
-   The GPU result wrapper exposes a structured-buffer SRV at `+0x8`: format
    `DXGI_FORMAT_UNKNOWN`, `D3D11_SRV_DIMENSION_BUFFER`, first element zero,
    4096 four-byte elements, backed by a buffer with
    `D3D11_RESOURCE_MISC_BUFFER_STRUCTURED`. Result value `1` means visible;
    `0` means occluded. This contract was measured in the retained guarded
    exterior capture and is validated before the resource becomes eligible.
-   The OBB render call in `Main::RenderDepth` is at RVA `0x1323250 + 0x3B1`.
    Its setup/render/restore sequence has completed before CSX marks the result
    available for later draw calls in the same D3D11 command stream.

## Experimental current-frame path

When `EnableCurrentFrameDepthCulling` is on:

1. Before native accumulation consumes an old result, CSX validates that the
   pointer belongs to one of the culler's two 4096-entry CPU arrays and changes
   that old result to visible. Native accumulation therefore keeps the complete
   eligible scene while still assigning current OBB slots.
2. Skyrim renders its normal stereo depth and runs its normal OBB GPU test.
3. After that test returns, CSX validates the exact structured SRV, underlying
   buffer, range, culler, both CPU-array identities, object count, CSX CPU
   frame, and engine depth generation. It retains COM ownership of the SRV and
   resource in an immutable producer token. A failed or repeated producer
   clears the previous token before attempting replacement.
4. During Lighting, grass, or distant-tree setup, CSX accepts an object only
   when its `+0x130` stamp equals the token's engine generation and its
   `+0x128` pointer maps to a slot inside the token's validated range. Setup
   stages a draw token tied to the exact render pass, geometry, producer token,
   and object index; it does not bind the SRV yet.
5. After Skyrim and CSX have committed dirty state, the matching draw token is
   armed. Immediately before the actual D3D11 `Draw*`, CSX saves vertex slot
   `t127`, binds the token SRV, reads the effective slot back, and enables the
   descriptor only when the exact view remains effective. Immediately after
   the draw, it restores the previous slot and clears the descriptor. Every
   mismatch fails visible.
6. The Lighting, grass, and distant-tree colour vertex shaders read current
   visibility before skinning, wind, transforms, or other vertex work. An
   occluded draw returns degenerate clip output immediately, preventing
   rasterisation and pixel shading. Their depth variants do not use the gate. A
   visible or ineligible draw follows the normal shader unchanged.

Particles, effects, water, reflection renders, and other non-opaque paths are
not gated. Objects that the engine did not submit to the current OBB test fail
the frame-stamp check and remain visible. This prevents stale slots from culling
transparent or otherwise excluded geometry.

## Fallback and failure rules

The experimental setting defaults off. Hook capability remains false unless
the required VR callsite is validated and the accumulation detour transaction
commits successfully. The frame mode, culler, context, setting, native toggle,
shader-cache state, and diagnostic control are latched together at
`EarlyPrepass`; a mid-frame change cannot create a mixed producer/consumer
mode. Turning the option off does not disable or replace Skyrim's native
delayed implementation.

Any missing culler, buffer, SRV, frame match, slot mapping, shader-cache state,
in-world state, render-pass identity, resource contract, or effective draw-time
binding fails visible. The previous `t127` binding is restored on every armed
draw exit. No temporal pose, rotation-speed, translation, or screen-edge
heuristic is retained.

## Validation status

The implementation that produced the earlier live measurements was reviewed as
PR5 and was found not to prove its own draw-time resource chain. In particular,
it could enable the descriptor after a rejected bind, did not retain an
immutable producer token, and allowed the graph builder to accept a slot
overwrite or a resource mismatch. Those live observations remain useful for
scene selection and native-resource discovery, but they do **not** qualify the
corrected path.

Completed for the corrected source:

-   exact structured-resource validation and COM-owned producer token;
-   frame-latched behavior and capability-gated hook installation;
-   final-`Draw*` scoped bind, exact readback verification, descriptor commit,
    and previous-slot restoration;
-   separate CSX CPU-frame and engine-generation evidence fields;
-   generation-bound asynchronous decision recording;
-   graph regressions for intervening `t127` overwrite, producer/view resource
    mismatch, and repeated producers in one frame pair;
-   a shader-source ownership check requiring the depth-culling include to be the
    sole declaration of `register(t127)`;
-   explicit Lighting `RENDER_DEPTH` and `RENDER_SHADOWMAP` shader exclusion,
    matching the existing grass and distant-tree depth exclusions;
-   the Release DevBench plugin and focused runtime, controller/serialization,
    and depth-diagnostics executables build and pass;
-   the render-graph builder regressions and render-map schema/contract suite
    pass;
-   all 158 shader-test assertions pass.

Historical pre-correction live observations:

-   attended Breezehome basement traversal with the option enabled showed no
    whole-field missing-object flashes at the known occlusion boundary;
-   a qualified null-HMD Bleakwind run observed roughly 2,020 current candidates
    per frame and about 55.6% occlusion;
-   two paired live-versus-forced-visible exterior samples showed a repeatable
    `0.20-0.24 ms` reduction in a broad instrumented span; the span included
    unrelated later work and cannot be attributed to the gated draw set;
-   a zero-drop one-frame render-map capture joined 1,691 current-frame consumers
    to 1,691 draws using the exact same result resource version;
-   all effective slot-127 bindings matched the requested view;
-   every matched Lighting draw was stereo-instanced with two instances, and the
    shader maps those instances to left and right;
-   no grass or distant-tree draw was visibility-bound in that exterior sample.

The corrected diagnostic build reports bind attempts and every fail-open
reason separately for Lighting, DistantTree, and Grass. It also makes snapshots
and resets epoch-safe. Its existing pipeline-statistics and coverage-span
fields remain explicitly legacy broad spans; they must not be used for a
feature-savings claim.

Detailed provenance and caveats are in
[`render-map/guarded-bleakwind-depth-culling-capture-2026-08-28.md`](./render-map/guarded-bleakwind-depth-culling-capture-2026-08-28.md).

Still required before promotion:

-   capture the corrected final-draw bind/restore chain and reject any
    `bindingRejected`, render-pass mismatch, token mismatch, or unaccounted bind;
-   inspect the D3D11 debug/RenderDoc state for SRV/UAV hazards;
-   replace or supplement the legacy broad query spans with exact draw-scoped
    measurements, then compare matched off, live, and forced-visible runs;
-   compare whole-frame GPU time and OBB-visible counts against native-on and
    native-off baselines in additional static scenes;
-   exercise alpha-tested foliage, water edges, particles, actors, interiors,
    exteriors, dynamic resolution, and Terrain Blending.
