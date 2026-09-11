# VR fog and reflection sampling

VR volumetric lighting and screen-space reflections clamp filtered samples
to physical texel centers inside the eye that owns each sample. The shared
`Stereo::ClampToEyeUV` texture-dimension overload handles packed textures;
`VRStereoEffects` adds texture-specific current/previous dynamic-resolution
bounds. Integer active extents determine those bounds, including fractional
requested scales and odd widths. Vertical coordinates retain their existing
sampler behavior.

Volumetric scattering uses each physical eye's world-space origin.
Horizontal blur dispatches separate groups for each eye, clamps its halo
reads to that eye, and excludes padded writes beyond the active extent.
Its C++ and HLSL constant-buffer layouts remain synchronized. The leading
four dimensions retain the non-VR and vertical-blur layout.

Reflection marching continues to resolve hits across eyes through
`ResolveMonoUVForEye`. Each depth, normal, color, mask, and history texture
supplies its own sampling bounds after the owning eye is resolved. Depth
dimensions are queried before the raymarch loop. The full-sized VR previous
depth texture retains its unscaled mapping.

Existing SSR foveation, godray opacity, rain suppression, and interior
controls remain effective. These corrections are automatic in VR and add
no settings or runtime feature switches. Flat shader behavior is preserved.

## Upstream provenance

The focused port incorporates
[Open Shaders #593](https://github.com/alandtse/open-shaders/pull/593)
(`2c1be1bc7d7496b6ac4d1367d004fb5462133c4e`, kevdevrev), plus the
texture-dimension clamp from
[#464](https://github.com/alandtse/open-shaders/pull/464)
(`18467b5671af7e3882fefea293a83a4ea4c4b938`, Dlizzio). The Post Processing
pipeline is outside this port's scope.

## Adversarial review and offline validation

The review checked scope, eye ownership, resource-specific bounds, current
versus previous scales, constant-buffer packing, padded blur dispatches,
existing controls, and reuse of shared helpers. It identified and resolved:

-   Fractional active extents displaced the upstream clamp from physical texel
    centers. The new fractional-width regression fails with the upstream
    formula and passes with integer-extent normalization.
-   Signed group division in the upstream blur produced FXC warning X3556.
    The synchronized group-count fields and division now use unsigned values.
-   Horizontal dispatch now reuses the group count computed in `EarlyPrepass`
    for both flat and VR rendering.

Evidence is retained locally under
`build/pr593-stereo-evidence-20260912/`:

-   Six new HLSL cases pass in the existing ShaderTestFramework runner. They
    cover even/odd widths, one-texel eyes, current/previous scales, fractional
    extents, interior UV preservation, and the flat identity path.
-   `compile-runtime.exe` compiles all six changed production shaders through
    `Util::CustomInclude`: 48 FXC SM5 combinations of flat/VR, HDR, SSR,
    volumetric lighting, lens flare, and cloud/terrain shadows pass with
    warnings treated as errors.
-   `verify-flat.ps1` invokes `tools/verify-shader-refactor.ps1` against
    `f2e77ca5c`; all 14 selected flat permutations have identical DXBC.
-   Both existing hlslkit validation configurations pass their 10 selected
    variants with zero errors and zero new warnings. Those configurations
    cover three of the six changed entry points; the explicit runtime-include
    compilation above covers all six.
-   `blur-regression.exe` executes the production horizontal blur on D3D11
    WARP at eight widths, including odd, tiny, and thread-group boundaries.
    All 11,600 checked pixels preserve separate constant eye values and
    untouched padding. The original shader, using its original dispatch
    count, fails 720 pixel checks in the same fixture.

In-game testing, visual/performance measurements, deployment, and
render-scale qualification were omitted at the user's explicit request.
Offline checks do not establish in-game visual quality or frame-time cost.
