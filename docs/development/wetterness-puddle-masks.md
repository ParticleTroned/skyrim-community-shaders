# Wetterness puddle masks

Wetterness offers four footprint-generation modes in its Puddles settings
and its existing performance settings panel. They change puddle placement,
not the weather simulation, drying model, wet BRDF, or cubemap schedule.

| Mode                  | Stored value | Footprint generation                                    |
| --------------------- | ------------ | ------------------------------------------------------- |
| Simple                | 0            | Slope only; no noise samples or distinct puddle islands |
| Textured (default)    | 1            | One lookup in a cached two-channel noise texture        |
| Textured High Quality | 2            | Two lookups with rotated/scaled coordinates             |
| Legacy Procedural     | 3            | Original 3D Perlin placement, including layout warp     |

Radius and Layout do not affect Simple mode. Textured modes retain these
controls but produce a different pattern from Legacy. The HQ lookup adds
variation; it is not an exact reconstruction of the procedural pattern.
All modes retain the existing wetness, material, slope, and shelter gates.

## Resource and shader contract

-   `src/Features/Wetterness/PuddleMask.cpp` generates deterministic periodic
    noise during feature resource setup. There is no per-frame generation,
    disk asset, compute dispatch, or new reflection capture.
-   The immutable 256 x 256 `R8G8_UNORM` texture has one mip and 128 KiB of
    texel data, excluding driver allocation overhead.
-   `features/Wetterness/Shaders/Wetterness/PuddleMask.hlsli` owns sampling
    and the original procedural implementation. Its only caller is the
    existing Wetterness surface calculation in `Lighting.hlsl`, under
    `WETTERNESS`.
-   Pixel-shader SRV slot 71 carries the mask. Prepass binds the resource
    while wetness is active and clears the slot when inactive. Sampling uses
    the existing material color sampler, explicit LOD 0, and wrapped UVs.
-   The mode occupies the previous final padding lane at byte offset 268 of
    the 272-byte Wetterness per-frame buffer. Following feature-buffer
    offsets do not change. The C++ and HLSL declarations must stay paired.
-   Wetterness feature version 1-7-1 accompanies the new shader contract.
    Update the DLL and shader package together.

No desktop/VR-specific path is added. Camera-relative coordinates are
converted back to world coordinates using the existing eye-indexed
camera adjustment.

## Settings and failure behavior

`PuddleMaskMode` is saved independently of the existing quality/climate
presets and is included in performance-state capture/restore. Reset to
defaults selects Textured. Missing or out-of-range mode values select
Textured.

For experimental configurations containing `EnableProceduralPuddleNoise`
but no `PuddleMaskMode`, false selects Simple and true selects Textured.
An explicit mode takes precedence.

If allocation or D3D resource creation fails, a warning is logged and the
effective shader mode falls back to Simple. The selected setting remains
unchanged and the menu reports the fallback. The remaining wetness effect
continues to run.

## Performance evidence

The procedural path performs up to two 3D Perlin evaluations per eligible
pixel: one for layout warp and one for the footprint. Textured modes
replace that work with one or two cached texture lookups; Simple bypasses
noise altogether.

Earlier in-game A/B testing reported unstable frame times with procedural
puddle noise enabled. That observation motivated this change; it is not a
measured per-pass GPU saving. No fixed millisecond or percentage speedup
is claimed.

For a controlled comparison, hold scene, camera, resolution, upscaler,
weather, and other feature settings fixed and switch only Puddle Mask.
Check dry ground, active rain, post-rain drying, shorelines, sheltered
surfaces, and camera movement. Verify desktop and VR separately. Legacy
remains available as the visual and performance reference.
