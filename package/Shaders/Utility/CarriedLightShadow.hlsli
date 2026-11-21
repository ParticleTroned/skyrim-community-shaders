#if defined(PSHADER)
#	include "Common/Permutation.hlsli"
#endif

namespace CarriedLightShadow
{
	static const float NearDistance = 12.0;
	float NormalizedNearDistance(float inverseRadius)
	{
		return isfinite(inverseRadius) && inverseRadius > 0 ? min(inverseRadius * NearDistance, NearDistance / 256.0) : 0;
	}

#if defined(PSHADER)
	bool RejectNearCaster(float distance, float cutoff)
	{
		return (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::CarriedShadowLight) != 0 && distance < cutoff;
	}
#endif
}
