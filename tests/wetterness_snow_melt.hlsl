#define main LightingMain
#include "Lighting.hlsl"
#undef main

RWStructuredBuffer<float4> Results : register(u0);

[numthreads(8, 1, 1)] void area_main(uint3 id : SV_DispatchThreadID) {
	static const float2 positions[8] = { float2(0, 0), float2(10, 0), float2(20, 0), float2(-5, -5), float2(10, 10), float2(5, 5), float2(6, 6), float2(2, 8) };
	float3 p = float3(positions[id.x], 0);
	float mask = SnowCover::GetAreaMask(p);
	SnowCover::CoverageInputs surface = SnowCover::GetSurfaceFootprint(float3(0, 0, 1), 0, p, mask, 1000, 0, 0, 0);
	SnowCover::CoverageInputs foliage = SnowCover::GetFoliageFootprint(float3(0, 0, 1), p, 0, 0, mask);
	float environment;
	float melt = 0;
#ifdef WETTERNESS
	melt = Wetterness::GetSnowMeltWetness(surface, float3(0, 0, 1)).x;
#endif
	Results[id.x] = float4(SnowCover::GetCoverage(surface, float3(1, 500, 0), 0),
		SnowCover::GetCoverage(foliage, float3(1, 500, 0), 0),
		SnowCover::GetFoliageSnowBase(float3(0, 0, 1), p, 0, mask, environment),
		melt);
}

#ifdef WETTERNESS
	[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID)
{
	SnowCover::CoverageInputs footprint = (SnowCover::CoverageInputs)0;
	footprint.environment = id.x == 0 ? -2 : (id.x == 1 ? -0.8 : (id.x == 3 ? -0.5 : 0));
	footprint.scale = id.x == 4 ? 0 : 1;
	footprint.position = float3(id.x == 5 ? 100 : 0, 0, 0);
	float3 weights = id.x == 6 ? float3(1, 0, 0) : (id.x == 7 ? float3(0, 1, 0) : float3(0, 0, 1));
	float2 wetness = Wetterness::GetSnowMeltWetness(footprint, weights);
	Results[id.x] = float4(wetness, WetternessDrying::Remaining(24, 24), 1);
}
#endif
