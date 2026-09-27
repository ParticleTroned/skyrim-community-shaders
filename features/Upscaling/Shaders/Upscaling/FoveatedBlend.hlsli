#ifndef UPSCALING_FOVEATED_BLEND_HLSLI
#define UPSCALING_FOVEATED_BLEND_HLSLI

#include "Common/FoveatedMask.hlsli"

float FoveatedComputeCurvedBlendWeight(float2 eyeUv, float centerScale, float centerFeather, float centerHorizontalScale, float2 centerOffset, float falloff)
{
	float weight = FoveatedComputeCenterBlendWeight(eyeUv, centerScale, centerFeather, centerHorizontalScale, centerOffset);
	// Preserve legacy arithmetic and the CPU-planned zero/full-weight regions.
	if (falloff == 1.0 || weight <= 0.0 || weight >= 1.0)
		return weight;

	float normalizedFeather = FoveatedComputeNormalizedFeather(centerScale, centerFeather, centerHorizontalScale);
	float maskDistance = FoveatedComputeMaskDistance(eyeUv, centerScale, centerHorizontalScale, centerOffset);
	float ramp = 1.0 - saturate((maskDistance - 1.0) / normalizedFeather);
	ramp = pow(ramp, clamp(falloff, 0.5, 2.0));
	return ramp * ramp * (3.0 - 2.0 * ramp);
}

#endif
