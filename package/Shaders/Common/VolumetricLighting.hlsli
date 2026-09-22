#ifndef __VOLUMETRIC_LIGHTING_DEPENDENCY_HLSL__
#define __VOLUMETRIC_LIGHTING_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"

namespace VolumetricLighting
{
	static const float kColorChannelMax = 65504.0;
	static const float kTuningEpsilon = 0.0001;
	static const float kSaturationMax = 4.0;
	static const float kOpacityMax = 2.0;

	bool IsFinite(float value)
	{
		// Inspect the input bits before FXC can fold finite checks into a clamp.
		return (asuint(value) & 0x7f800000) != 0x7f800000;
	}

	float3 SanitizeColor(float3 color)
	{
		return float3(
			IsFinite(color.r) ? clamp(color.r, 0.0, kColorChannelMax) : 0.0,
			IsFinite(color.g) ? clamp(color.g, 0.0, kColorChannelMax) : 0.0,
			IsFinite(color.b) ? clamp(color.b, 0.0, kColorChannelMax) : 0.0);
	}

	/** @brief Shape shaft visibility after temporal blending without altering weather density. */
	float ApplyOpacity(float value, float rawOpacity)
	{
		float positiveValue = IsFinite(value) ? max(value, 0.0) : 0.0;
		float opacity = IsFinite(rawOpacity) ? clamp(rawOpacity, 0.0, kOpacityMax) : 1.0;
		if (opacity <= kTuningEpsilon)
			return 0.0;
		if (abs(opacity - 1.0) <= kTuningEpsilon)
			return positiveValue;

		float boundedValue = saturate(positiveValue);
		float shapedValue = 1.0 - pow(max(1.0 - boundedValue, 0.0), opacity);
		return shapedValue + max(positiveValue - 1.0, 0.0) * opacity;
	}

	/** @brief Tune the engine-resolved weather color at composition without changing weather records. */
	float3 ApplyColor(float3 authoredColor, float saturation, float4 customColor)
	{
		saturation = IsFinite(saturation) ? clamp(saturation, 0.0, kSaturationMax) : 1.0;
		float contribution = IsFinite(customColor.w) ? saturate(customColor.w) : 0.0;
		float3 color = SanitizeColor(authoredColor);
		if (abs(saturation - 1.0) <= kTuningEpsilon && contribution <= kTuningEpsilon)
			return color;

		if (abs(saturation - 1.0) > kTuningEpsilon) {
			float luminance = Color::RGBToLuminance(color);
			color = Color::Saturation(color, saturation);
			float saturatedLuminance = Color::RGBToLuminance(color);
			// Clipping negative channels must not brighten shafts or cap HDR chroma at its input peak.
			color *= saturatedLuminance > 0.0 ? luminance / saturatedLuminance : 0.0;
			float peak = max(color.r, max(color.g, color.b));
			if (peak > kColorChannelMax) {
				// Fit the half-float range along the constant-luminance line instead of clipping brightness.
				float chromaScale = saturate((kColorChannelMax - luminance) / (peak - luminance));
				color = lerp(luminance.xxx, color, chromaScale);
			}
		}

		// Keep small custom channels when weather HDR is much brighter than the replacement.
		precise float3 blendedColor = color * (1.0 - contribution) + saturate(SanitizeColor(customColor.rgb)) * contribution;
		return SanitizeColor(blendedColor);
	}
}

#endif
