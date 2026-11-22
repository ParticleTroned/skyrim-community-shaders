#ifndef GRASS_BATCH_HLSLI
#define GRASS_BATCH_HLSLI

#include "GrassOptimizations/GrassInstance.hlsli"

cbuffer GrassBatch : register(b9)
{
	uint GrassBatchEnabled;  // Bits select batching, far-billboard lighting and compact source records.
	uint GrassBatchEye;
	uint GrassBatchBase;
	float GrassBatchCollisionDistance;
	float4 GrassBatchOrigin;
};

StructuredBuffer<GrassInstanceExtra> GrassInstanceExtras : register(t2);
ByteAddressBuffer GrassSourceRecords : register(t3);
StructuredBuffer<float> GrassTextureNormalLength : register(t4);

float GetGrassBatchTextureNormalLength()
{
	float normalLength = 0;
	[branch] if (GrassBatchEnabled) normalLength = GrassTextureNormalLength[0];
	return normalLength;
}

void LoadGrassBatchInstance(inout float4 data1, inout float4 data2, inout float4 data3, inout float4 data4)
{
	[branch] if (GrassBatchEnabled)
	{
		uint address = uint(data1.x) * 16;
		uint seed;
		LoadGrassInstance(GrassSourceRecords, address, (GrassBatchEnabled & 4u) != 0, data1, data2, data3, data4, seed);
	}
}

float GetGrassBatchFade(uint instanceId)
{
	return abs(GrassInstanceExtras[GrassBatchBase + instanceId].originFade.w);
}

float GetGrassBatchShading(uint instanceId)
{
	// Negative values select cheaper shading; -2 is reserved for far billboards.
	return (GrassBatchEnabled & 2u) ? -2 : (GrassInstanceExtras[GrassBatchBase + instanceId].originFade.w < 0 ? -1 : 0);
}

float3 GetGrassBatchOffset(uint instanceId)
{
	return GrassBatchEnabled ? GrassInstanceExtras[GrassBatchBase + instanceId].originFade.xyz - GrassBatchOrigin.xyz : 0;
}

#endif
