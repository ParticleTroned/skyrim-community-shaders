#ifndef HAIR_SCATTERING_HLSLI
#define HAIR_SCATTERING_HLSLI

#include "Common/Math.hlsli"

namespace HairScattering
{
	/** @brief Evaluates diffuse scattering with a finite axial-view limit. */
	float DiffuseFactor(float3 tangent, float3 view, float3 light, float tangentDotLight, float tangentDotView)
	{
		float3 projectedView = view - tangent * tangentDotView;
		// An axial view has no preferred azimuth; zero retains only the symmetric wrap.
		float3 fakeNormal = projectedView * rsqrt(max(dot(projectedView, projectedView), EPSILON_LENGTH_SQ));
		float wrappedNdotL = saturate((dot(fakeNormal, light) + 1) * 0.25);
		float diffuseKajiya = saturate(1 - abs(tangentDotLight));
		return (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
	}
}

#endif
