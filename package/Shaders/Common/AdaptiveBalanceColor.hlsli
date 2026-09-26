#ifndef __ADAPTIVE_BALANCE_COLOR_HLSLI__
#define __ADAPTIVE_BALANCE_COLOR_HLSLI__

#include "Common/Color.hlsli"

namespace AdaptiveBalanceColor
{
	/// Grades the composed scene in linear light before fades and display encoding.
	float3 Apply(float3 color, float contrast, float saturation, bool linearLighting)
	{
		// Neutral profiles must retain the original path without a gamma round trip.
		[branch] if (contrast != 1.0 || saturation != 1.0)
		{
			// Display encoding uses absolute values; preserve signed intermediates for fades.
			const float3 colorSign = color < 0.0 ? -1.0 : 1.0;
			float3 linearColor = abs(color);
			if (!linearLighting)
				linearColor = Color::GammaToLinearSafe(linearColor);

			if (contrast != 1.0) {
				const float middleGray = 0.18;
				const float luminance = Color::RGBToLuminance(linearColor);
				// A luminance curve preserves RGB ratios and avoids a hard shadow cutoff.
				const float safeLuminance = max(luminance, 1e-6);
				linearColor *= pow(safeLuminance / middleGray, contrast - 1.0);
			}
			if (saturation != 1.0)
				linearColor = Color::Saturation(linearColor, saturation);

			color = colorSign * (linearLighting ? linearColor : Color::LinearToGammaSafe(linearColor));
		}
		return color;
	}
}

#endif
