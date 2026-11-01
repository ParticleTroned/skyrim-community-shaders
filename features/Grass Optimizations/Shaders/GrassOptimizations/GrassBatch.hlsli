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
ByteAddressBuffer GrassSourceRecords : register(t3);

void LoadGrassBatchInstance(inout float4 data1, inout float4 data2, inout float4 data3, inout float4 data4)
{
	[branch] if (GrassBatchEnabled)
	{
		uint address = uint(data1.x) * 32;
		uint4 a = GrassSourceRecords.Load4(address);
		uint4 b = GrassSourceRecords.Load4(address + 16);
		data1 = float4(UnpackGrassInstance(a.x), UnpackGrassInstance(a.y));
		data2 = float4(UnpackGrassInstance(a.z), UnpackGrassInstance(a.w));
		data3 = float4(UnpackGrassInstance(b.x), UnpackGrassInstance(b.y));
		data4 = float4(UnpackGrassInstance(b.z), UnpackGrassInstance(b.w));
	}
}

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
