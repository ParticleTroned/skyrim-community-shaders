#define LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS
#include "Common/Color.hlsli"
#include "IBL/IBL.hlsli"

SamplerState SampColorSampler : register(s0);
#include "DynamicCubemaps/DynamicCubemaps.hlsli"
RWTexture2D<float4> Results : register(u0);

[numthreads(1, 1, 1)] void main() {
	float3 normal = float3(0, 0, 1);
	float3 ambient = Color::Ambient(max(0, SharedData::GetAmbient(normal)));
	Results[uint2(0, 0)] = float4(Color::ApplyAmbientBalance(ambient), 1);
	Results[uint2(1, 0)] = float4(Color::ApplyAmbientBalance(ImageBasedLighting::GetDiffuseIBL(ambient, normal)), 1);
	Results[uint2(2, 0)] = float4(Color::ApplyAmbientBalance(ImageBasedLighting::GetDiffuseIBLOccluded(ambient, normal, 0.35)), 1);
	float3 specular = ImageBasedLighting::ComputeSpecularIBL(
		DynamicCubemaps::EnvTexture, DynamicCubemaps::EnvReflectionsTexture, SampColorSampler, normal, 0,
		Color::RGBToLuminance(ambient), 0.35, 0.35);
	Results[uint2(3, 0)] = float4(Color::ApplyAmbientBalanceLinear(specular), 1);
	Results[uint2(4, 0)] = float4(Color::DirectionalLight(float3(0.2, 0.3, 0.4), true), 1);
	Results[uint2(5, 0)] = float4(ImageBasedLighting::GetIBLRatio(), 1);
	Results[uint2(6, 0)] = float4(ambient, 1);
	Results[uint2(7, 0)] = float4(Color::Glowmap(float3(0.1, 0.2, 0.3)), 1);
	Results[uint2(8, 0)] = float4(DynamicCubemaps::ComputeSpecularIrradiance(normal, 0, Color::RGBToLuminance(ambient), 0.35, 0.35), 1);
	Results[uint2(9, 0)] = float4(ImageBasedLighting::GetFogIBLColor(float3(0.2, 0.3, 0.4)), 1);
}
