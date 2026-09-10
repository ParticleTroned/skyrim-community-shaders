# Motion-adaptive DLSS sharpening

The optional **Motion-adaptive sharpening** control adjusts RCAS strength
using current unjittered engine motion vectors. It is disabled by default.
The existing fixed RCAS shader and its strength curve remain the default.
This feature applies to DLSS/DLAA with RCAS selected. FSR retains its own
integrated sharpening; Luma Unsharp is unchanged.

## Controls

| Control              | Default | Range                       | Meaning                                                                                                                                                          |
| -------------------- | ------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Enabled              | false   | boolean                     | Enable motion adjustment after DLSS.                                                                                                                             |
| Motion adjustment    | -0.5    | -1 to 1                     | Add this amount to normalized sharpness at sufficiently fast motion. Negative values reduce sharpening during head/camera movement; positive values increase it. |
| Motion threshold     | 2       | 0 to 64 output pixels/frame | Start adjusting above this speed. Reach the full adjustment after a further `max(threshold, 1)` pixels/frame.                                                    |
| Motion sharpness cap | 1       | 0 to 1                      | Maximum adjusted strength, using the same scale as the ordinary Sharpness slider.                                                                                |

For example, base sharpness 0.7, adjustment -0.5, and threshold 2 gives
strength 0.7 at 2 pixels/frame, 0.45 at 3 pixels/frame, and 0.2 at
4 pixels/frame and above. Strength is clamped between zero and the cap,
then converted through the existing RCAS curve. Zero disables sharpening
for the affected pixel. Setting the base Sharpness slider to zero disables
the entire sharpening pass, including positive motion adjustment.

The cap also limits stationary pixels when it is below the base strength.
Motion uses displacement per rendered frame, so the same camera speed can
produce different adjustment at different frame rates. No frame-rate
normalization or temporal smoothing is applied.

Settings persist through the existing Save Settings operation as
`motionAdaptiveRCAS`, `motionSharpnessAdjustment`,
`motionSharpnessThreshold`, and `motionSharpnessCap`. Older configuration
files retain disabled defaults. Nonfinite settings loaded from a file use
finite defaults; out-of-range values are clamped before conversion to
shader floats, including finite JSON numbers larger than a float can hold.
Malformed optional values are logged and replaced with their field defaults;
an invalid enable value disables the option. Unrelated upscaling settings
still load normally.

## Input and failure behavior

The source is raw engine motion rather than the vendor input encoder's
partially populated foveated intermediates. Engine motion is normalized
to one complete eye; the packed texture width is not its normalization
width. A source rectangle, full source-eye rectangle, and final output
rectangle describe every dispatch. Output pixel centers map to source
texels inside the source rectangle. Motion-to-output conversion scales by
`output extent * full source-eye extent / source crop extent` separately
for each axis.

Flat SE/AE output uses one region. Packed VR main-pass output uses two
bounded eye regions. Submit-stage output uses its corresponding source
depth/motion rectangle, including crop offsets, and one final eye region.
Foveated vendor output is sharpened after composition, using the original
full-eye motion. Both color neighbors and motion reads remain inside the
current eye/crop. Geometry is validated against the actual textures before
dispatch; ambiguous or invalid mappings use fixed RCAS.

History-reset and menu frames use fixed RCAS. Submit-stage motion also
requires a current producer proof and an ordinary scene source. A missing
motion SRV, shader creation failure, or dispatch failure retains fixed
sharpening. Nonfinite motion values fall back to the fixed strength for
that pixel, including bypassing the optional cap. The settings never alter
vendor contexts or history-reset policy.

The optional shader is preloaded with RCAS when enabled at resource setup.
Enabling it later can compile it on its first eligible dispatch. A failed
shader/setup attempt is logged and held until the shader cache is cleared.
Compute shader, class instances, constant buffer, SRVs, and UAV state are
restored with scope-based cleanup around the optional pass. No additional
color or motion textures are allocated.
The prior compute output is unbound before binding inputs so an overlapping
UAV cannot make D3D11 silently replace the color or motion SRV with null.

## DevBench

`communityshaders.menu` accepts:

```json
{
    "action": "set_motion_adaptive_sharpening",
    "enabled": true,
    "adjustment": -0.5,
    "thresholdPixels": 2.0,
    "strengthCap": 1.0,
    "expectedBuildId": "<exact producer Build ID>"
}
```

All four settings are required. Invalid types, nonfinite numbers, and
out-of-range values reject the request before narrowing to floats or
changing settings. Values just outside a bound cannot round into range. The
setter stages settings in memory and returns `persisted: false`; Save
Settings persists them. It does not select DLSS or switch the sharpener.

The setter and `status` expose `motionAdaptiveSharpening` with configured
values, `applicable`, and `lastRCASDispatch`. Applicability means DLSS/DLAA,
RCAS, and nonzero base sharpness are selected; `enabled` is independent.
The last dispatch is historical, not a claim that the current frame has
finished. Values are `not_dispatched`, `disabled`, `applied`,
`fixed_motion_unavailable`, `fixed_invalid_geometry`,
`fixed_shader_unavailable`, or `fixed_dispatch_failed`.

## Validation

-   `MotionSharpeningPolicy` covers settings bounds/nonfinite handling,
    packed eyes, cropped submit geometry, unavailable/overflowing bounds,
    and normalized-eye motion conversion to output pixels.
-   `SharpenerBindings` exercises the production binding helper on D3D11
    WARP with color, motion, output and empty prior UAV bindings, for both
    adaptive and fixed sharpening, while preserving an unrelated UAV slot.
-   `MotionSharpeningSettings` feeds malformed optional fields through the
    production normalizer and JSON deserialization, checking unrelated
    settings, missing defaults, huge values and serialization round trips.
-   `TestMotionSharpening.hlsl` covers the fixed strength curve, zero
    strength, signed adjustment, threshold/cap behavior, invalid motion,
    output-to-source mapping, stereo edge clamps, and cropped motion units.
-   The fixed RCAS DXBC is identical to `ef7c366dd` for the flat, VR, HDR,
    and VR+HDR permutations using `tools/verify-shader-refactor.ps1`.
-   The optional permutation compiles with FXC `cs_5_0`, `/Ges`, and `/WX`
    for flat, VR, HDR, and VR+HDR.

In-game visual/performance comparisons and VR render-scale qualification
remain required validation before release. They were not run while
preparing this change. There is no measured performance or visual-quality
claim. Compare disabled, negative, and positive adjustment in a controlled
scene with fine foliage, distant edges, water, head rotation, and moving
objects; retain both eyes and all fallback classifications.

## Acknowledgments

OptiScaler's motion-adaptive RCAS work informed this feature. References
are pinned to commit `d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec`:
[RCAS motion handling](https://github.com/optiscaler/OptiScaler/blob/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec/OptiScaler/shaders/rcas/shaders/RCAS_Shader.h#L60-L98)
and [configuration](https://github.com/optiscaler/OptiScaler/blob/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec/OptiScaler/Config.h#L312).
Thanks to the OptiScaler contributors for the idea and reference design.

The motion policy, coordinate mapping, and shader helpers here are an
independent implementation inspired by OptiScaler; no OptiScaler source
was copied. The existing AMD FidelityFX RCAS implementation and its AMD
copyright/MIT license notice remain in `RCAS.hlsl`.
