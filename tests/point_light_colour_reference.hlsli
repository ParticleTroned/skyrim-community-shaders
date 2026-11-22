// Frozen inline point-light formula used to validate compute preparation.
// Keep this oracle independent of the production point-light and utility cores.
#include "Common/Color.hlsli"

namespace PointLightReference
{
	float3 Light(float3 color, bool isLinear = false, bool preserveHDRIntensity = false)
	{
		[branch] if (ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear)
		{
			if (preserveHDRIntensity) {
				const float hdrScale = max(1.0f, max(abs(color.r), max(abs(color.g), abs(color.b))));
				color = Color::CategoryGamma(color / hdrScale, SharedData::linearLightingSettings.lightGamma) * hdrScale;
			} else {
				color = Color::CategoryGamma(color, SharedData::linearLightingSettings.lightGamma);
			}
		}
#if defined(TRUE_PBR)
		return color * Color::PBRLightingCompensation;  // Compensate for traditional Lambertian diffuse
#else
		return color;
#endif
	}

	float GetPointLightMultiplier(bool isLinear)
	{
		return isLinear ? SharedData::adaptiveBalanceSettings.linearPointLightMult : SharedData::adaptiveBalanceSettings.pointLightMult;
	}

	float GetPointLightTypeMultiplier(bool isLinear, uint lightFlags)
	{
		if ((lightFlags & Color::PointLightFlagSpot) != 0)
			return isLinear ? SharedData::adaptiveBalanceSettings.linearSpotlightMult : SharedData::adaptiveBalanceSettings.spotlightMult;
		if ((lightFlags & Color::PointLightFlagOmnidirectionalBulb) != 0)
			return isLinear ? SharedData::adaptiveBalanceSettings.linearOmnidirectionalBulbMult : SharedData::adaptiveBalanceSettings.omnidirectionalBulbMult;
		return 1.0f;
	}

	bool IsSceneColorDraw()
	{
#if defined(PSHADER) && defined(LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS)
		return (Permutation::ExtraShaderDescriptor & (Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::InReflection)) != 0;
#else
		return true;
#endif
	}

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
				color = Color::Saturation(color, saturation);
			color = AdaptiveBalanceColorBounds::Intermediate(color, linearSpace);
		}
		return color;
	}

	float3 AdjustUtilityColor(float3 color, float saturation, float gammaOffset)
	{
		return AdjustUtilityColor(color, saturation, gammaOffset, ENABLE_LL_COLOR_ADJUSTMENTS);
	}

	/// Select the light-type colour controls composed once during upload.
	float2 PointLightUtilityAdjustment(bool isLinear, uint lightFlags)
	{
		float2 adjustment = float2(1.0, 0.0);
		[branch] if (SharedData::adaptiveBalanceSettings.appearance.pointLightColorEnabled)
		{
			SharedData::AdaptiveBalanceAppearanceSettings appearance = SharedData::adaptiveBalanceSettings.appearance;
			adjustment = isLinear ? float2(appearance.linearPointLightSaturation, appearance.linearPointLightGammaOffset) :
			                        float2(appearance.nonlinearPointLightSaturation, appearance.pointLightGammaOffset);
#if CSX_PERF_REFERENCE_FLAGS & CSX_PERF_POINT_LIGHT_COMPOSITION
			float2 subtype = float2(1.0, 0.0);
#else
			float2 subtype = adjustment;
#endif
			if ((lightFlags & Color::PointLightFlagSpot) != 0)
				subtype = isLinear ? float2(appearance.linearSpotlightSaturation, appearance.linearSpotlightGammaOffset) :
				                     float2(appearance.spotlightSaturation, appearance.spotlightGammaOffset);
			else if ((lightFlags & Color::PointLightFlagOmnidirectionalBulb) != 0)
				subtype = isLinear ? float2(appearance.linearOmnidirectionalBulbSaturation, appearance.linearOmnidirectionalBulbGammaOffset) :
				                     float2(appearance.omnidirectionalBulbSaturation, appearance.omnidirectionalBulbGammaOffset);
#if CSX_PERF_REFERENCE_FLAGS & CSX_PERF_POINT_LIGHT_COMPOSITION
			adjustment = float2(clamp(adjustment.x * subtype.x, 0.0, 2.0), clamp(adjustment.y + subtype.y, -1.0, 1.0));
#else
			adjustment = subtype;
#endif
		}
		return adjustment;
	}

	/// Composed Adaptive Balance multiplier, excluding out-of-world object previews.
	float3 PointLight(float3 color, bool isLinear = false, uint lightFlags = 0, bool preserveHDRIntensity = false)
	{
		const float curve = SharedData::adaptiveBalanceSettings.pointLightCurve;
		const float saturation = SharedData::adaptiveBalanceSettings.pointLightSaturation;
		const float2 utility = PointLightUtilityAdjustment(isLinear, lightFlags);
		const bool grade = IsSceneColorDraw() && (curve != 1.0 || saturation != 1.0 || utility.x != 1.0 || utility.y != 0.0);
		// Grade authored RGB before light conversion; neutral settings retain signed inputs.
		[branch] if (IsSceneColorDraw() && curve != 1.0)
			color = pow(max(color, 0.0), curve);
		color = Light(color, isLinear, preserveHDRIntensity);
		color = AdjustUtilityColor(color, utility.x, utility.y);
		if (grade)
			color = AdaptiveBalanceColorBounds::Intermediate(color, ENABLE_LL_COLOR_ADJUSTMENTS);
		// Neutral profiles preserve the authored color without a gamma round trip.
		[branch] if (IsSceneColorDraw() && saturation != 1.0)
		{
			float3 linearColor = ENABLE_LL_COLOR_ADJUSTMENTS ? color : Color::GammaToLinearSafe(color);
			linearColor = Color::Saturation(linearColor, saturation);
			color = ENABLE_LL_COLOR_ADJUSTMENTS ? linearColor : Color::LinearToGammaSafe(linearColor);
		}
		color *= ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f);
		if (IsSceneColorDraw())
			color = color * GetPointLightMultiplier(isLinear) * GetPointLightTypeMultiplier(isLinear, lightFlags);
		return grade ? AdaptiveBalanceColorBounds::Output(color) : color;
	}

	float3 PointLightPreserveHDRIntensity(float3 color, bool isLinear = false, uint lightFlags = 0)
	{
		return PointLight(color, isLinear, lightFlags, true);
	}

}
