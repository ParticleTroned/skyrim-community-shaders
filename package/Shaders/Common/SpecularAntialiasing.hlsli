#ifndef SPECULAR_ANTIALIASING_HLSLI
#define SPECULAR_ANTIALIASING_HLSLI

namespace SpecularAntialiasing
{
	static const float PixelFilterVariance = 1.0 / 12.0;  // Unit-width box filter.
	static const float MaximumSlopeVariance = 0.04;       // Bound broadening at normal discontinuities.

	/// Compute a bounded GGX filter kernel from derivatives of a unit surface normal.
	float GetKernelVariance(float3 normalDx, float3 normalDy)
	{
		float gradientSquared = dot(normalDx, normalDx) + dot(normalDy, normalDy);
		return min(2.0 * PixelFilterVariance * gradientSquared, MaximumSlopeVariance);
	}

	/// Broaden perceptual roughness in GGX slope space using a kernel from GetKernelVariance.
	float FilterRoughness(float roughness, float kernelVariance)
	{
		roughness = saturate(roughness);
		float roughnessSquared = roughness * roughness;
		return max(roughness, sqrt(sqrt(saturate(roughnessSquared * roughnessSquared + kernelVariance))));
	}
}

#endif
