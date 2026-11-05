#ifndef WETTERNESS_DRYING_HLSLI
#define WETTERNESS_DRYING_HLSLI

#include "Common/WetternessDrying.hlsli"

namespace Wetterness
{
	float3 GetSurfaceDryingWeights(float3 baseColor, float roughness)
	{
		float vegetation = smoothstep(0.06, 0.30, baseColor.g - max(baseColor.r, baseColor.b));
		float stone = saturate((0.62 - roughness) * 2.4) * (1.0 - vegetation);
		return float3(stone, vegetation, saturate(1.0 - vegetation - stone));
	}

	float RemainingDryingFraction(float elapsedHours, float durationHours)
	{
		return WetternessDrying::Remaining(max(0.0, elapsedHours), max(0.001, durationHours));
	}
}

#endif
