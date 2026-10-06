#ifndef GRASS_BATCH_HLSLI
#define GRASS_BATCH_HLSLI

#include "GrassOptimizations/GrassInstance.hlsli"

cbuffer GrassBatch : register(b9)
{
	uint GrassBatchEnabled;
	uint GrassBatchEye;
	uint GrassBatchBase;
	float GrassBatchCollisionDistance;
	float4 GrassBatchOrigin;
};

StructuredBuffer<GrassInstanceExtra> GrassInstanceExtras : register(t2);

float GetGrassBatchFade(uint instanceId)
{
	return abs(GrassInstanceExtras[GrassBatchBase + instanceId].originFade.w);
}

bool GetGrassBatchSimpleShading(uint instanceId)
{
	return GrassBatchEnabled && GrassInstanceExtras[GrassBatchBase + instanceId].originFade.w < 0;
}

float3 GetGrassBatchOffset(uint instanceId)
{
	return GrassBatchEnabled ? GrassInstanceExtras[GrassBatchBase + instanceId].originFade.xyz - GrassBatchOrigin.xyz : 0;
}

#endif
