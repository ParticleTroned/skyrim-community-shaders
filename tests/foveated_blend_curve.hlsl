#include "Upscaling/FoveatedBlend.hlsli"

cbuffer Samples : register(b0)
{
	float4 Geometry;  // x=centerScale, y=feather, z=horizontalScale
};
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	float2 uv = (float2(id.x % 128, id.y) + 0.5) / 128.0;
	float2 offset = float2(id.x < 128 ? -0.0625 : 0.0625, 0.03125);
	float legacy = FoveatedComputeCenterBlendWeight(uv, Geometry.x, Geometry.y, Geometry.z, offset);
	float neutral = FoveatedComputeCurvedBlendWeight(uv, Geometry.x, Geometry.y, Geometry.z, offset, 1.0);
	float low = FoveatedComputeCurvedBlendWeight(uv, Geometry.x, Geometry.y, Geometry.z, offset, 0.5);
	float high = FoveatedComputeCurvedBlendWeight(uv, Geometry.x, Geometry.y, Geometry.z, offset, 2.0);
	Output[id.xy] = float4(legacy, neutral, low, high);
}
