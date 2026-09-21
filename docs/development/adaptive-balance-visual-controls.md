# Adaptive Balance sky and water controls

Adaptive Balance exposes sky saturation in the shared Global Lighting
baseline and in each detailed profile. Water caustics and parallax controls
are available in each profile's detailed Water controls.

| Control                   | Neutral | Range         |
| ------------------------- | ------- | ------------- |
| Sky Saturation            | 1       | 0-2           |
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

Disabling Adaptive Balance publishes neutral sky and water values without
discarding the saved adjustments.

## Runtime validation

In an exterior scene with visible sky and water, verify that Sky Saturation
zero produces a monochrome sky and one restores the neutral result. With
Water Effects and water parallax active, verify each zero-value bypass,
confirm that Parallax Quality changes sample detail without destabilizing
grazing flowmap views, and disable then re-enable Adaptive Balance to confirm
that saved values return.
