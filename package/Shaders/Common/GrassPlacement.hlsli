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
#ifdef ADAPTIVE_BALANCE
	float2 offsets = GrassPlacementOffsets;
#else
	float2 offsets = 0;
#endif
#ifdef GRASS_OPTIMIZATIONS
	[branch] if (GrassBatchEnabled)
		// The current translation is already part of the batch origin.
		offsets = float2(0, GrassInstanceExtras[GrassBatchBase + instanceID].previousPlacementDelta);
#endif
	return offsets;
}
#endif
