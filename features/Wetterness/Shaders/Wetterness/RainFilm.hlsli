#ifndef WETTERNESS_RAIN_FILM_HLSLI
#define WETTERNESS_RAIN_FILM_HLSLI

namespace Wetterness
{
	/** Converts the persisted 0..2.5 ground-film scale to coverage without early saturation. */
	float GetRainFilmAmount(float exposedWetness, float groundFilmScale)
	{
		return saturate(exposedWetness) * saturate(groundFilmScale * (1.0 / 2.5));
	}

	/** The coating retains material response while its strength controls the blend with the dry surface. */
	float GetRainFilmRoughness(float materialRoughness, float specularFloorScale)
	{
		float roughnessLimit = lerp(0.40, 0.22, saturate(specularFloorScale * (1.0 / 3.0)));
		return clamp(materialRoughness * 0.5, 0.18, roughnessLimit);
	}

	/** Fade the correction into puddle and shoreline contributions without a coverage threshold. */
	float GetRainFilmWeight(float filmAmount, float otherWetness, float nonPuddleMask)
	{
		return saturate(filmAmount / max(max(filmAmount, otherWetness), 1e-3)) * saturate(nonPuddleMask);
	}

	/** The existing glare control changes peak compression, independently of coating roughness. */
	float GetRainFilmHighlightReduction(float control, float filmWeight)
	{
		return sqrt(saturate((control - 0.25) / 9.75)) * filmWeight;
	}
}

#endif
