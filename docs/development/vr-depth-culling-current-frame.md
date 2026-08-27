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

- `0xDA1860` is the per-object accumulation function. It reads the pointer at
  `NiAVObject+0x128`, accepts it only when the frame stamp at `+0x130` is one
  engine frame old, and can suppress the object before assigning its new OBB
  slot. This proves that native CPU culling consumes a previous-frame result.
- The culler pointer is stored at `0x36F1870`; the engine frame counter is at
  `0x3186C5C`.
- The culler holds at most 4096 OBBs. Its object count is at `+0xB0`, its two
  CPU result-array pointers are at `+0xD0` and `+0xD8`, and its GPU result
  buffer wrapper is at `+0x100`.
- The GPU result wrapper exposes an `R32_UINT` SRV at `+0x8`. Result value `1`
  means visible; `0` means occluded.
- The OBB render call in `Main::RenderDepth` is at RVA `0x1323250 + 0x3B1`.
  Its setup/render/restore sequence has completed before CSX marks the result
  available for later draw calls in the same D3D11 command stream.

## Experimental current-frame path

When `EnableCurrentFrameDepthCulling` is on:

1. Before native accumulation consumes an old result, CSX validates that the
   pointer belongs to one of the culler's two 4096-entry CPU arrays and changes
   that old result to visible. Native accumulation therefore keeps the complete
   eligible scene while still assigning current OBB slots.
2. Skyrim renders its normal stereo depth and runs its normal OBB GPU test.
3. After that test returns, CSX records readiness for the current CSX frame.
4. During Lighting setup, CSX accepts an object only when its `+0x130` stamp
   equals the current engine frame and its `+0x128` pointer maps to a valid
   current OBB slot below the current object count.
5. CSX packs that 12-bit slot into otherwise-unused high bits of the existing
   per-draw shader descriptor and binds the engine's live visibility SRV at
   vertex slot `t127`. A dedicated descriptor bit enables the shader path only
   for that validated draw.
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

The experimental setting defaults off. Its descriptor bit is cleared for every
Lighting draw unless every current-frame invariant succeeds, so the high SRVs
are never read by the fallback path. Turning the option off does not disable or
replace Skyrim's native delayed implementation.

Any missing culler, buffer, SRV, frame match, slot mapping, shader-cache state,
or in-world state fails visible. No temporal pose, rotation-speed, translation,
or screen-edge heuristic is retained.

## Validation status

Completed locally:

- VR Release plugin build with warnings treated as errors;
- representative VR Lighting vertex permutations, including skinned,
  landscape, model-space-normal, and tree variants;
- representative grass and distant-tree colour permutations, with their depth
  variants remaining byte-for-byte identical;
- pixel permutations remain byte-for-byte identical;
- assembly inspection confirms that `t127` is read only inside the
  descriptor-gated branch and that an occluded draw returns before normal
  vertex work;
- all 155 shader unit-test assertions pass.

Still required on a physical Skyrim VR run:

- confirm startup and stable world rendering with the option off;
- enable the option and verify current-frame culling without alternating or
  missing geometry during head motion;
- inspect the D3D11 debug/RenderDoc state for SRV/UAV hazards;
- compare GPU frame time, pixel/vertex workload, and OBB-visible counts against
  native-on and native-off baselines;
- exercise alpha-tested foliage, water edges, particles, actors, interiors,
  exteriors, dynamic resolution, and Terrain Blending.
