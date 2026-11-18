#ifndef CSX_FOLIAGE_SNOW_HLSLI
#define CSX_FOLIAGE_SNOW_HLSLI

#include "Common/LightingCommon.hlsli"

#if defined(TRUE_PBR)
namespace Foliage
{
	float3 GetUncoveredColor(MaterialProperties material)
	{
#	if defined(SNOW_COVER) && (defined(TREE_ANIM) || defined(PBR_GRASS))
		if (material.FoliageSnowCoverage > 0)
			return material.FoliageBaseColor * (1 - material.FoliageSnowCoverage);
#	endif
		return material.BaseColor;
	}

#	if defined(SNOW_COVER)
	void ApplySnowCoating(inout MaterialProperties material, float coverage, float3 foliageColor)
	{
		coverage = saturate(coverage);
#		if defined(TREE_ANIM) || defined(PBR_GRASS)
		material.FoliageBaseColor = foliageColor;
		material.FoliageSnowCoverage = coverage;
#		endif
		material.Thickness = lerp(material.Thickness, 1, coverage);
		material.FuzzWeight *= 1 - coverage;
		material.CoatStrength *= 1 - coverage;
	}

#		if defined(TREE_ANIM)
	void DarkenSnowSubstrate(inout MaterialProperties material, float amount, float blend)
	{
		// Later wetness must affect surviving leaf light continuously as snow vanishes.
		if (material.FoliageSnowCoverage > 0 && material.FoliageSnowCoverage < 1)
			material.FoliageBaseColor = lerp(material.FoliageBaseColor, pow(abs(material.FoliageBaseColor), 1 + amount), blend);
	}
#		endif
#	endif
}
#endif

#endif
