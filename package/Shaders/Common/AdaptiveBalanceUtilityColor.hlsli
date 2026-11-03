#ifndef __ADAPTIVE_BALANCE_UTILITY_COLOR_HLSLI__
#define __ADAPTIVE_BALANCE_UTILITY_COLOR_HLSLI__

// Included inside Color after the colour-space helpers and scene-draw gate.

/// Adjust working-space RGB while leaving neutral values and object previews untouched.
float3 AdjustUtilityColor(float3 color, float saturation, float gammaOffset, bool linearSpace)
{
	[branch] if (IsSceneColorDraw() && (saturation != 1.0 || gammaOffset != 0.0))
	{
		color = AdaptiveBalanceColorBounds::Intermediate(color, linearSpace);
		if (gammaOffset != 0.0) {
			const float baseGamma = linearSpace ? 2.2 : 1.0;
			const float exponent = clamp(baseGamma + gammaOffset, 0.01, 4.0) / baseGamma;
			color = sign(color) * pow(abs(color), exponent);
		}
		if (saturation != 1.0)
			color = Saturation(color, saturation);
		color = AdaptiveBalanceColorBounds::Intermediate(color, linearSpace);
	}
	return color;
}

float3 AdjustUtilityColor(float3 color, float saturation, float gammaOffset)
{
	return AdjustUtilityColor(color, saturation, gammaOffset, ENABLE_LL_COLOR_ADJUSTMENTS);
}

/// Compose subtype adjustments with the existing shared point-light grading.
float2 PointLightUtilityAdjustment(bool isLinear, uint lightFlags)
{
	float2 adjustment = float2(1.0, 0.0);
	[branch] if (SharedData::adaptiveBalanceSettings.appearance.pointLightColorEnabled)
	{
		SharedData::AdaptiveBalanceAppearanceSettings appearance = SharedData::adaptiveBalanceSettings.appearance;
		adjustment = isLinear ? float2(appearance.linearPointLightSaturation, appearance.linearPointLightGammaOffset) :
		                        float2(appearance.nonlinearPointLightSaturation, appearance.pointLightGammaOffset);
		float2 subtype = float2(1.0, 0.0);
		if ((lightFlags & PointLightFlagSpot) != 0)
			subtype = isLinear ? float2(appearance.linearSpotlightSaturation, appearance.linearSpotlightGammaOffset) :
			                     float2(appearance.spotlightSaturation, appearance.spotlightGammaOffset);
		else if ((lightFlags & PointLightFlagOmnidirectionalBulb) != 0)
			subtype = isLinear ? float2(appearance.linearOmnidirectionalBulbSaturation, appearance.linearOmnidirectionalBulbGammaOffset) :
			                     float2(appearance.omnidirectionalBulbSaturation, appearance.omnidirectionalBulbGammaOffset);
		adjustment = float2(clamp(adjustment.x * subtype.x, 0.0, 2.0), clamp(adjustment.y + subtype.y, -1.0, 1.0));
	}
	return adjustment;
}

/// Grade effect lighting before its influence and material texture are applied.
float3 WeatherEffectLighting(float3 color, bool skyObject)
{
	return AdjustUtilityColor(color,
		skyObject ? SharedData::adaptiveBalanceSettings.appearance.skyStaticSaturation : SharedData::adaptiveBalanceSettings.appearance.effectSaturation,
		skyObject ? SharedData::adaptiveBalanceSettings.appearance.skyStaticGammaOffset : SharedData::adaptiveBalanceSettings.appearance.effectGammaOffset);
}

#endif
