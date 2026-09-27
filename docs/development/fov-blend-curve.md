# FOV blend curve

In the in-game **VR > FOV > Upscaling FOV Controls**, enable
**FOV Blend Curve** to test a different center/periphery fade. The
**FOV Blend Falloff** slider ranges from 0.5 to 2.0:

-   Off or 1.0 uses the existing feather calculation.
-   Below 1.0 gives the center more weight inside the transition band.
-   Above 1.0 gives the periphery more weight inside that band.

The checkbox defaults to off. Turning it off retains the selected exponent
for comparison; Save Settings persists both controls. Old configurations
remain off with exponent 1.0. The controls apply to FOV-only and FOV + TAA
when VR foveated upscaling is active. A nonneutral curve is a visual
preference; no quality or performance advantage has been measured.

This is the falloff-only adaptation of
[Open Shaders #778](https://github.com/alandtse/open-shaders/pull/778).
The power-4 mask, per-eye offsets, feather width, CPU dispatch rectangles,
underlay holes and tile coverage retain their existing geometry. Other
features sharing the FOV mask retain their original feathering. No ellipse
selector or dither mode is added.

## Runtime behavior

The upscaler-specific HLSL helper returns the original result at exponent
1 and at zero/full blend weight. Otherwise it applies the exponent to the
inward ramp before smoothstep. All three consumers use the same helper:
center blending, spatial composition and periphery TAA. Main and submit
routes use these same dispatch helpers.

Center blending and spatial composition reuse constant-buffer padding.
Periphery TAA appends one float4, growing its buffer from 320 to 336 bytes;
existing field offsets remain unchanged. C++ layout assertions and the
shader reflection fixture cover these contracts. Upscaling's shader
feature version advances to 2-6-0.

Settings are sanitized to finite values in [0.5,2], with nonfinite values
falling back to 1. The setter requests a history reset and invalidates
frame state only when the effective exponent changes. History tracking
also detects loaded settings changes, and finalized submit-eye reuse
compares the effective exponent. Curve changes do not change resource
dimensions or the FSR resource lifecycle key. SE/AE remain neutral, reject
the VR setter and omit these VR settings when saving.

## DevBench

Use `communityshaders.menu` with these arguments:

```json
{ "action": "set_fov_blend_curve", "enabled": true, "falloff": 0.5 }
```

```json
{ "action": "set_fov_blend_curve", "enabled": false }
```

`enabled` must be a boolean. Optional `falloff` must be finite and within
[0.5,2]; omission preserves the saved value. Invalid values reject the
update before mutation. This action requires loaded VR Upscaling, does not
enable FOV and does not save settings. `status.fov.blendCurve` and the
setter response expose the checkbox, saved exponent, effective exponent
and mask applicability. They describe configuration, not proof of a
completed draw.

## Validation

Source extraction and structural checks can run without a build. The
`FovSettings` controller fixture covers defaults, the actual ImGui checkbox,
disabled slider, value preservation, finite-value handling, DevBench
validation, runtime gating and history/frame invalidation. The
`FoveatedBlendCurveShader` WARP fixture compiles the three production
consumers and reflects their buffer layouts, then samples the production
helper for neutral equality, zero/full ownership, finite bounds, falloff
direction and mirrored eye offsets in flat and VR permutations.

The deferred end-of-sync universal DLL build and both compiled fixtures
passed as part of all 160 registered tests; exact source, Build ID and
the known periphery-cache shader warning handling are preserved in the
[validation report](open-shaders-dev-validation.md). Deployment and
SE/AE/VR in-game checks have not run. Physical-HMD
`csx-render-scale-pr-v1` qualification and comparable performance evidence
remain pending. No in-game result or ledger measurement is claimed.
