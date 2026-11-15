#ifndef __ADAPTIVE_BALANCE_FIRE_HLSLI__
#define __ADAPTIVE_BALANCE_FIRE_HLSLI__

#include "Common/Color.hlsli"
#include "Common/Permutation.hlsli"

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace AdaptiveBalanceFire
{
	/// Recognizes unlit additive palette effects and indexed flames.
	bool IsFire()
	{
#	if defined(ADDBLEND) && !defined(MOTIONVECTORS_NORMALS) && !defined(LIGHTING) && !defined(MEMBRANE) && !defined(BLOOD) && !defined(PROJECTED_UV)
#		if defined(PARTICLES) && defined(TEXCOORD_INDEX) && defined(INDEXED_TEXTURE) && !defined(SOFT)
		return true;
#		else
		const uint paletteFlags = Permutation::EffectFlags::GrayscaleToColor | Permutation::EffectFlags::GrayscaleToAlpha;
		return (Permutation::PixelShaderDescriptor & paletteFlags) == paletteFlags;
#		endif
#	else
		return false;
#	endif
	}

	/// Keeps glow grading on sky sprites and soft plain-textured glows, separate from fire.
	bool IsLightSprite()
	{
#	if defined(ADDBLEND) && !defined(MOTIONVECTORS_NORMALS) && !defined(LIGHTING) && !defined(MEMBRANE) && !defined(BLOOD) && !defined(PROJECTED_UV)
		const bool skyObject = (Permutation::VertexShaderDescriptor & Permutation::EffectFlags::SkyObject) != 0;
#		if defined(SOFT) && defined(TEXTURE)
		const uint nonPlainFlags = Permutation::EffectFlags::GrayscaleToColor | Permutation::EffectFlags::GrayscaleToAlpha | Permutation::EffectFlags::IgnoreTexAlpha;
		return !IsFire() && (skyObject || (Permutation::PixelShaderDescriptor & nonPlainFlags) == 0);
#		else
		return !IsFire() && skyObject;
#		endif
#	else
		return false;
#	endif
	}

	/// Grades visible scene flames in linear light without changing their opacity.
	float3 Apply(float3 color)
	{
		const float intensity = SharedData::adaptiveBalanceSettings.fireIntensity;
		const float saturation = SharedData::adaptiveBalanceSettings.fireSaturation;
		const float curve = SharedData::adaptiveBalanceSettings.fireCurve;
		// Neutral profiles retain authored RGB exactly, including signed intermediates.
		[branch] if (Color::IsSceneColorDraw() && IsFire() &&
					 (intensity != 1.0 || saturation != 1.0 || curve != 1.0))
		{
			if (intensity == 0.0) {
				color = 0.0;
			} else {
				const float3 colorSign = color < 0.0 ? -1.0 : 1.0;
				float3 linearColor = abs(color);
				if (!ENABLE_LL_COLOR_ADJUSTMENTS)
					linearColor = Color::GammaToLinearSafe(linearColor);
				if (curve != 1.0) {
					// A luminance curve separates bright cores from dim edges without shifting hue.
					const float luminance = max(Color::RGBToLuminance(linearColor), 1e-6);
					linearColor *= pow(luminance, curve - 1.0);
				}
				linearColor = AdaptiveBalanceColorBounds::Intermediate(linearColor, true);
				if (saturation != 1.0)
					linearColor = Color::Saturation(linearColor, saturation);
				linearColor *= intensity;
				color = AdaptiveBalanceColorBounds::Output(colorSign * (ENABLE_LL_COLOR_ADJUSTMENTS ? linearColor : Color::LinearToGammaSafe(linearColor)));
			}
		}
		return color;
	}
}
#endif

#endif
