#ifndef GRASS_BATCH_HLSLI
#define GRASS_BATCH_HLSLI

cbuffer GrassBatch : register(b9)
{
	uint GrassBatchEnabled;
	uint GrassBatchEye;
	uint GrassBatchBase;
	uint GrassBatchPadding;
	float4 GrassBatchOrigin;
};

StructuredBuffer<float4> GrassInstanceExtras : register(t2);

float GetGrassBatchFade(uint instanceId)
{
	return abs(GrassInstanceExtras[GrassBatchBase + instanceId].w);
}

bool GetGrassBatchSimpleShading(uint instanceId)
{
	return GrassBatchEnabled && GrassInstanceExtras[GrassBatchBase + instanceId].w < 0;
}

float3 GetGrassBatchOffset(uint instanceId)
{
	return GrassBatchEnabled ? GrassInstanceExtras[GrassBatchBase + instanceId].xyz - GrassBatchOrigin.xyz : 0;
}

#endif
