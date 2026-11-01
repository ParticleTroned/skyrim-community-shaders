#ifndef WETTERNESS_HIGHLIGHTS_HLSLI
#define WETTERNESS_HIGHLIGHTS_HLSLI

namespace Wetterness
{
	/** Keep the local-light lobe broad throughout a dark night, including flat ripple phases. */
	float GetPointLightRoughness(float roughness, float darkRippleMask, float darkNightFactor)
	{
		const float minPointLightRoughness = 0.18;
		return lerp(roughness, max(roughness, minPointLightRoughness), max(darkRippleMask, darkNightFactor));
	}

	/** Avoid cancellation at exact alignment while the nighttime roughness floor blends in. */
	float GetPointLightDistribution(float roughness, float NdotH)
	{
		const float a = roughness * roughness;
		const float a2 = a * a;
		const float NdotH2 = NdotH * NdotH;
		// The saturation boundary keeps the small positive term separate without propagating precise.
		const float sinThetaSquared = saturate(1.0 - NdotH2);
		const float denominator = max(sinThetaSquared + NdotH2 * a2, a2);
		return a2 / (Math::PI * denominator * denominator);
	}

	/** Compress only the wet lobe's peak; its light colour, shadow and attenuation remain multiplicative. */
	float GetPointLightHighlightScale(float response, float darkNightFactor)
	{
		const float peakKnee = 0.25;
		const float peakLimit = 1.0;
		if (darkNightFactor <= 0.0 || response <= peakKnee)
			return 1.0;
		const float excess = response - peakKnee;
		const float compressed = peakKnee + excess / (1.0 + excess / (peakLimit - peakKnee));
		return lerp(1.0, compressed / response, darkNightFactor);
	}
}

#endif
