#define EPSILON_DIVISION 1e-5
#include "../package/Shaders/Common/ProjectedMaterial.hlsli"
#include "../package/Shaders/Common/Triplanar.hlsli"

Texture2D<float4> Material : register(t0);
SamplerState MaterialSampler : register(s0);
cbuffer TestInput : register(b0)
{
	uint Route;
	uint Plane;
	float Scale;
	float Bias;
	uint Displaced;
};

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 PositionOnPlane(float2 uv)
{
	return Plane == 0 ? float3(0, uv) : (Plane == 1 ? float3(uv.x, 0, uv.y) : float3(uv, 0));
}

float4 PSMain(float4 pixel : SV_Position) : SV_Target
{
	float2 uv = 0.0625 + pixel.xy / 32;
	float2 offset = float2(Displaced != 0 ? (uint(pixel.x) & 1) * 0.5 : 0, 0);
	float3 original = PositionOnPlane(uv);
	float3 aligned = original + ProjectedMaterial::GetParallaxPositionOffset(offset, ddx(uv), ddy(uv), ddx(original), ddy(original));
	float3 weights = Plane == 0 ? float3(1, 0, 0) : (Plane == 1 ? float3(0, 1, 0) : float3(0, 0, 1));
	float4 actual;
	if (Route == 0)
		actual = Triplanar::Sample(Material, MaterialSampler, aligned, weights, Scale, original);
	else if (Route == 1)
		actual = Triplanar::SampleStochastic(Material, MaterialSampler, aligned, weights, Scale, 0.5, original);
	else if (Route == 2)
		actual = Triplanar::SampleStochasticBias(Material, MaterialSampler, aligned, weights, Scale, Bias, 0.5, original);
	else
		actual = Triplanar::SampleStochastic(Material, MaterialSampler, aligned, weights, Scale, 0.5);

	// The texture has eight texels per UV unit, while the receiver spans 1/32 UV per pixel.
	float lod = max(0, log2(abs(Scale) / 4) + (Route == 2 ? Bias : 0));
	float4 expected = Material.SampleLevel(MaterialSampler, (uv + offset) * Scale, lod);
	return actual - expected;
}
