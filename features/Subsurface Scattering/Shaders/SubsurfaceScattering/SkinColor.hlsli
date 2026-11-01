#ifndef __SSS_SKIN_COLOR_HLSLI__
#define __SSS_SKIN_COLOR_HLSLI__

#include "Common/Color.hlsli"

// Saturation changes chroma; brightness and scattering intensity own luminance.
float3 BurleySkinColor(float3 original, float3 scattered, float intensity, float brightness, float baseSaturation, float finalSaturation, float mask)
{
	float3 neutral = max(AdaptiveBalanceColorBounds::Output(original + (scattered * brightness - original) * intensity), 0.0f);
	[branch] if (mask > 0.0f && (baseSaturation != 1.0f || finalSaturation != 1.0f))
	{
		float grey = Color::RGBToLuminance(scattered);
		float3 base = lerp(grey.xxx, scattered, baseSaturation) * brightness;
		// Intensity extrapolation can leave negative RGB before the final saturation.
		float3 adjusted = max(original + (base - original) * intensity, 0.0f);
		grey = Color::RGBToLuminance(adjusted);
		adjusted = max(lerp(grey.xxx, adjusted, finalSaturation), 0.0f);

		// Preserve energy after gamut clipping and in Skyrim's non-linear lighting mode.
		float3 neutralLinear = Color::IrradianceToLinear(neutral);
		float3 adjustedLinear = Color::IrradianceToLinear(adjusted);
		float neutralLuminance = Color::RGBToLuminance(neutralLinear);
		float adjustedLuminance = Color::RGBToLuminance(adjustedLinear);
		// Restore missing luminance as grey instead of amplifying near-black chroma.
		float luminanceScale = neutralLuminance / max(max(adjustedLuminance, neutralLuminance), FLT_MIN);
		adjustedLinear = adjustedLinear * luminanceScale + max(neutralLuminance - adjustedLuminance, 0.0f);
		float limit = Color::IrradianceToLinear(AdaptiveBalanceColorBounds::kOutputMax);
		float peak = max(adjustedLinear.r, max(adjustedLinear.g, adjustedLinear.b));
		[branch] if (peak > limit)
		{
			// Fit the storage gamut along a constant-luminance line, preserving HDR light.
			float chromaScale = saturate((limit - neutralLuminance) / max(peak - neutralLuminance, FLT_MIN));
			adjustedLinear = lerp(neutralLuminance.xxx, adjustedLinear, chromaScale);
		}
		// A nearly transparent skin/hairline sample must not receive full-strength grading.
		neutral = AdaptiveBalanceColorBounds::Output(Color::IrradianceToGamma(lerp(neutralLinear, adjustedLinear, saturate(mask))));
	}
	return neutral;
}

#endif
