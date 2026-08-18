# VR Depth-Culling Temporal Validity

## Summary

Skyrim VR could briefly omit foreground geometry during head rotation, exposing
bright fog, sky, or the clear colour for one frame. The resulting flash was most
obvious in fog-heavy interiors and could occur near the centre of the headset
view. Still screenshots did not reliably capture it, while headset video did.

This was not a preset, shader-compilation, streaming-codec, bitrate, video
buffering, or reduced-FOV problem. The fault was the lifetime of the engine's
depth-culling result.

## Engine behaviour

The VR oriented-bounding-box (OBB) occlusion pass runs every frame and evaluates
both eyes. Visibility from either eye keeps an object visible. Its GPU result is
read back and consumed when the following frame's scene is accumulated.

That one-frame pipeline is valid only while the consuming view remains coherent
with the view that produced the result. During HMD rotation, a zero from the old
view could be interpreted as "occluded" in the new view. Foreground geometry was
then omitted for one frame even though it had become visible.

An exact current-frame test cannot be consumed during current-frame scene
accumulation without restructuring the render pipeline: the depth occluders must
already have been selected and rendered before the OBB result exists.

## Correction

The OBB test and depth culling remain enabled and run every frame. At OBB render
time, CSX records the camera pose that generated the result. After the normal
next-frame readback, it accepts an occluded result only when the current view is
coherent with that recorded pose:

- rotation delta no greater than 0.05 degrees;
- translation delta no greater than 0.1 Skyrim world unit.

When the view is not coherent, only zero results are promoted to visible. Positive
results are left unchanged, and the current frame still performs a fresh OBB
test for the following frame. This is conservative for correctness: rapid view
changes may render objects that the old view considered occluded, while a stable
view retains normal depth-culling behaviour.

The implementation is VR-only in `src/Features/VR.cpp`. The related
`FrameAnnotations` OBB hook signature was corrected to match the live engine
call, whose second argument is a 32-bit value rather than a pointer.

## Validation

Validated on 18 August 2026 with:

- a physical HMD under the SteamVR/OpenVR runtime;
- `coc AbandonedPrison01` (Abandoned Prison), chosen for its strong interior fog;
- repeated rapid head rotation, including geometry/fog boundaries that had
  produced frequent central and peripheral flashes;
- depth culling left enabled throughout the test.

The prior build produced many white or bright-blue missing-geometry flashes in
this scene. With the temporal-validity correction, the same test produced no
white bits or flashes.

Validated DLL SHA-256:
`1A30F3583DE81367210757A2CEBF5DA19C00FD21F4EA39E4505950F8E11E0B55`.

This result establishes the correction for the tested SteamVR/OpenVR path. It
does not by itself establish equivalence for SteamVR OpenXR, VDXR, or other
OpenXR runtimes; those remain separate comparison targets.
