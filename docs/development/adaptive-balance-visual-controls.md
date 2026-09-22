# Adaptive Balance ambient, sky and water controls

Adaptive Balance exposes sky saturation in the shared Global Lighting
baseline and Ambient in each detailed profile. Water caustics and parallax
controls are available in each profile's detailed Water controls.

| Control                   | Neutral | Range         |
| ------------------------- | ------- | ------------- |
| Sky Saturation            | 1       | 0-2           |
| Ambient                   | 1       | 0-3           |
| Caustics Strength         | 1       | 0-2           |
| Caustics Tiling           | 1       | 0.25-4        |
| Caustics Speed            | 1       | 0-3           |
| Caustics Color Dispersion | 1       | 0-2           |
| Parallax Strength         | 1       | 0-2           |
| Parallax Quality          | 16      | 4-64, integer |

Zero sky saturation produces a grayscale sky. Zero caustics strength
disables caustics, zero speed freezes their animation, and zero dispersion
removes color separation. Zero parallax strength disables water parallax.
Higher parallax quality increases the sample count. Caustics require Water
Effects, and parallax requires its water-parallax option.

The shared sky saturation baseline multiplies the active profile value.
Exterior day/night transitions interpolate the composed sky result and all
water controls; parallax quality is rounded to the nearest integer. Interior
and location override profiles use their selected values directly. All
values are bounded before reaching shared shader data, and missing saved
fields take neutral defaults.

Wave Amplitude remains in the Water Surface group. It is independent of the
new caustics and parallax controls.

Disabling Adaptive Balance publishes neutral ambient, sky, and water values
without discarding the saved adjustments.

## Ambient

Ambient scales the combined vanilla or image-based ambient contribution.
It applies after IBL's environment and sky mix and DALC matching, including
when DALC Amount is zero. Matching uses the original ambient input, so the
balance multiplier is not applied twice. Linear Lighting retains its
independent ambient gamma and multiplier.

The global baseline, active profile, location adjustment, and Scene
Brightness response compose before reaching the shader. The result is
applied once to diffuse ambient and ambient reflections; linear-space
reflections use the corresponding converted multiplier. One preserves the
current lighting, while zero removes that ambient contribution. Direct
lights, emissive materials, and separately computed SSGI bounce lighting
retain their own controls. Inventory previews retain their existing
lighting.

IBL-derived fog receives the same adjustment before optional luminance
preservation. Water refraction does not reapply Ambient to the already-lit
scene texture.

## Runtime validation

In exterior and interior scenes, verify Ambient at zero and one with IBL
both enabled and disabled, and confirm inventory previews remain unchanged.
With visible sky and water, verify that Sky Saturation zero produces a
monochrome sky and one restores the neutral result. With Water Effects and
water parallax active, verify each zero-value bypass, confirm that Parallax
Quality changes sample detail without destabilizing grazing flowmap views,
and disable then re-enable Adaptive Balance to confirm that saved values
return.
