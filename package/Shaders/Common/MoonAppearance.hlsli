#ifndef __MOON_APPEARANCE_HLSLI__
#define __MOON_APPEARANCE_HLSLI__

#include "Common/Color.hlsli"
#include "Common/Permutation.hlsli"

namespace MoonAppearance
{
	/** Grade native moon draws and optionally clear texture borders, preserving neutral draws and previews. */
	float4 Apply(float4 color, float2 uv)
	{
		const float curve = SharedData::adaptiveBalanceSettings.appearance.moonCurve;
		const float trim = SharedData::adaptiveBalanceSettings.appearance.moonEdgeTrim;
		[branch] if ((curve != 1.0 || trim > 0.0) &&
					 (Permutation::ExtraShaderDescriptor & (Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::InReflection)) &&
					 (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::IsMoon))
		{
			color.rgb = AdaptiveBalanceAppearance::ApplyColor(color.rgb, 1.0, curve, 1.0, 1.0.xxx);
			[branch] if (trim > 0.0)
			{
				const float2 edge = abs(uv * 2.0 - 1.0);
				if (max(edge.x, edge.y) > 1.0 - trim * 0.02)
					color = 0.0;
			}
		}
		return color;
	}
}

#endif
