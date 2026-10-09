#ifndef __ADAPTIVE_BALANCE_APPEARANCE_HLSLI__
#define __ADAPTIVE_BALANCE_APPEARANCE_HLSLI__

// Included by Color.hlsli after its base colour-space functions.

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace AdaptiveBalanceAppearance
{
	/// Grade RGB without changing alpha; light helpers defer the scene range limit until after intensity.
	float3 ApplyColor(float3 color, float intensity, float curve, float saturation, float3 tint, bool sceneOutput = true)
	{
		[branch] if (Color::IsSceneColorDraw() &&
					 (intensity != 1.0 || curve != 1.0 || saturation != 1.0 || any(tint != 1.0)))
		{
			if (intensity == 0.0) {
				color = 0.0;
			} else {
				if (curve != 1.0)
					color = pow(max(color, 0.0), curve);
				color = AdaptiveBalanceColorBounds::Intermediate(color, ENABLE_LL_COLOR_ADJUSTMENTS);
				if (saturation != 1.0) {
					float3 linearColor = ENABLE_LL_COLOR_ADJUSTMENTS ? color : Color::GammaToLinearSafe(color);
					linearColor = Color::Saturation(linearColor, saturation);
					color = ENABLE_LL_COLOR_ADJUSTMENTS ? linearColor : Color::LinearToGammaSafe(linearColor);
				}
				color *= tint * intensity;
				color = sceneOutput ? AdaptiveBalanceColorBounds::Output(color) : AdaptiveBalanceColorBounds::Intermediate(color, ENABLE_LL_COLOR_ADJUSTMENTS);
			}
		}
		return color;
	}

	/// Grade the selected ambient lighting before multiplying material albedo.
	float3 ApplyAmbientSaturation(float3 color, bool linearSpace = false)
	{
		const float saturation = SharedData::adaptiveBalanceSettings.appearance.ambientSaturation;
		[branch] if (Color::IsSceneColorDraw() && saturation != 1.0)
		{
			float3 linearColor = linearSpace ? color : Color::IrradianceToLinear(color);
			linearColor = Color::Saturation(linearColor, saturation);
			color = linearSpace ? linearColor : Color::IrradianceToGamma(linearColor);
		}
		return color;
	}

	/// Grade fog after selecting weather or image-based color and before opacity blending.
	float3 ApplyFog(float3 color)
	{
		return ApplyColor(color, SharedData::adaptiveBalanceSettings.appearance.fogBrightness,
			1.0, 1.0, SharedData::adaptiveBalanceSettings.appearance.fogTint);
	}
}

namespace Color
{
	/// Grade authored RGB before conversion; optional attenuation retains the light-space response.
	float3 DirectionalLight(float3 color, bool isLinear = false, float attenuation = 1.0)
	{
		color = AdaptiveBalanceAppearance::ApplyColor(color, 1.0,
			SharedData::adaptiveBalanceSettings.appearance.directionalCurve, 1.0, 1.0.xxx, false);
		color = Light(color * attenuation, isLinear);
		const bool grade = IsSceneColorDraw() &&
		                   (SharedData::adaptiveBalanceSettings.appearance.directionalCurve != 1.0 ||
							   SharedData::adaptiveBalanceSettings.appearance.directionalSaturation != 1.0 ||
							   any(SharedData::adaptiveBalanceSettings.appearance.directionalTint != 1.0));
		if (grade)
			color = AdaptiveBalanceColorBounds::Intermediate(color, ENABLE_LL_COLOR_ADJUSTMENTS);
		color = AdaptiveBalanceAppearance::ApplyColor(color, 1.0, 1.0,
			SharedData::adaptiveBalanceSettings.appearance.directionalSaturation,
			SharedData::adaptiveBalanceSettings.appearance.directionalTint, false);
		color = color * ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
		        SharedData::adaptiveBalanceSettings.directionalLightMult;
		return grade ? AdaptiveBalanceColorBounds::Output(color) : color;
	}

	/// Apply once after ambient sources are combined, in the renderer's lighting space.
	float3 ApplyAmbientBalance(float3 color)
	{
		return AdaptiveBalanceAppearance::ApplyAmbientSaturation(color) * AmbientBalanceMultiplier();
	}

	/// Match composed ambient adjustments for contributions already in linear space.
	float3 ApplyAmbientBalanceLinear(float3 color)
	{
		return AdaptiveBalanceAppearance::ApplyAmbientSaturation(color, true) * IrradianceToLinear(AmbientBalanceMultiplier());
	}
}
#endif

#endif
