#ifndef GRASS_BATCH_HLSLI
#define GRASS_BATCH_HLSLI

cbuffer GrassBatch : register(b9)
{
	uint GrassBatchEnabled;
	uint3 GrassBatchPadding;
};

StructuredBuffer<float> GrassInstanceFades : register(t2);

float GetGrassBatchFade(uint instanceId)
{
#ifdef VR
	// Native VR layouts repeat each instance record for the two eye invocations.
	instanceId /= 2;
#endif
	return GrassInstanceFades[instanceId];
}

#endif
