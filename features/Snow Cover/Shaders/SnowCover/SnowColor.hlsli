#ifndef SNOW_COVER_COLOR_HLSLI
#define SNOW_COVER_COLOR_HLSLI

#include "Common/Color.hlsli"

namespace SnowCover
{

	bool UsesLandscapeSnowColor(bool alternate)
	{
		return (SharedData::snowCoverSettings.LandscapeSnowColorMask & (alternate ? 2u : 1u)) != 0;
	}

	/** Returns diffuse in the target path's lighting space, preserving installed source colors. */
	float3 SnowDiffuse(float3 albedo, bool alternate, bool pbrMaterial, bool foliage = false, bool applyDiffuseControls = true)
	{
		float3 diffuse = 0;
		[branch] if (UsesLandscapeSnowColor(alternate))
		{
			const bool sourcePbr = (SharedData::snowCoverSettings.LandscapeSnowColorMask & (alternate ? 8u : 4u)) != 0;
			diffuse = Color::Diffuse(albedo, sourcePbr);
			if (sourcePbr)
				diffuse *= Color::PBRLightingScale;
			// PBR surfaces apply this scale after lighting; foliage has no later scale.
			if (pbrMaterial && !foliage)
				diffuse /= Color::PBRLightingScale;
		}
		else
		{
			diffuse = pbrMaterial ? albedo : Color::LinearToSrgb(albedo);
			if (applyDiffuseControls)
				diffuse = Color::Diffuse(diffuse, pbrMaterial);
			if (!pbrMaterial || foliage)
				diffuse *= Color::PBRLightingScale;
		}
		return diffuse;
	}

}

#endif
