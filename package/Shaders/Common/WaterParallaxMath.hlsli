#ifndef WATER_PARALLAX_MATH_HLSLI
#define WATER_PARALLAX_MATH_HLSLI

#include "Common/WaterFinite.hlsli"
namespace WaterParallaxMath
{
	float Intersect(float bound, float height, float previousHeight, float stepSize)
	{
		if (height > bound)
			return bound;
		float previousBound = bound - stepSize;
		float delta2 = previousBound - previousHeight;
		float delta1 = bound - height;
		float denominator = delta2 - delta1;
		return WaterFinite::IsFinite(denominator) && abs(denominator) > 1e-7 ?
			(bound * delta2 - previousBound * delta1) / denominator : bound;
	}

	uint Budget(float detail)
	{
		return WaterFinite::IsFinite(detail) ? (uint)ceil(lerp(8.0, 64.0, saturate(detail))) : 64u;
	}
}
#endif
