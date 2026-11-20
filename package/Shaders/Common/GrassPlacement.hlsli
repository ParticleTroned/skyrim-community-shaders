#ifndef GRASS_PLACEMENT_HLSLI
#define GRASS_PLACEMENT_HLSLI

#ifdef ADAPTIVE_BALANCE
cbuffer GrassPlacement : register(b10)
{
	float2 GrassPlacementOffsets;
	float2 GrassPlacementPadding;
};
#endif

float2 GetGrassPlacementOffsets(uint instanceID)
{
#ifdef GRASS_OPTIMIZATIONS
	if (GrassBatchEnabled)
		// The current translation is already part of the batch origin.
		return float2(0, GrassInstanceExtras[GrassBatchBase + instanceID].previousPlacementDelta);
#endif
#ifdef ADAPTIVE_BALANCE
	return GrassPlacementOffsets;
#else
	return 0;
#endif
}
#endif
