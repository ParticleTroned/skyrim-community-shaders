#define main LightingMain
#include "Lighting.hlsl"
#undef main

RWStructuredBuffer<float4> Results : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	SnowCover::CoverageInputs footprint = (SnowCover::CoverageInputs)0;
	footprint.environment = id.x == 0 ? -2 : (id.x == 1 ? -0.8 : (id.x == 3 ? -0.5 : 0));
	footprint.scale = id.x == 4 ? 0 : 1;
	footprint.position = float3(id.x == 5 ? 100 : 0, 0, 0);
	float3 weights = id.x == 6 ? float3(1, 0, 0) : (id.x == 7 ? float3(0, 1, 0) : float3(0, 0, 1));
	float2 wetness = Wetterness::GetSnowMeltWetness(footprint, weights);
	Results[id.x] = float4(wetness, WetternessDrying::Remaining(24, 24), 1);
}
