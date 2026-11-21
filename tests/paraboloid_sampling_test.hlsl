#include "Common/Random.hlsli"

Texture2DArray<float4> TexShadowMapSampler : register(t3);
Texture2DArray<float4> TexShadowMapSamplerComp : register(t4);
SamplerState SampShadowMapSampler : register(s3);
SamplerComparisonState SampShadowMapSamplerComp : register(s4);
#include "Utility/ParaboloidSampling.hlsli"

cbuffer TestParameters : register(b0)
{
	float4 Cases[256];  // UV, filter radius, lower hemisphere.
	uint Dual;
};

float4 VertexMain(uint vertex : SV_VertexID) : SV_POSITION
{
	return float4(float2((vertex << 1) & 2, vertex & 2) * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PixelMain(float4 position : SV_POSITION) : SV_TARGET
{
	const float4 input = Cases[uint(position.x)];
	const float visibility = SampleParaboloidShadow(input.xy, 0, 0.5, float2x2(0.6, 0.8, -0.8, 0.6), input.z, Dual != 0, input.w != 0);
	const ParaboloidShadow::SamplingBounds bounds = ParaboloidShadow::GetSamplingBounds(TexShadowMapSampler, Dual != 0, input.w != 0);
	const float2 uv = ParaboloidShadow::ClampTap(input.xy, bounds);
	return float4(visibility, uv, bounds.rimRadius);
}
