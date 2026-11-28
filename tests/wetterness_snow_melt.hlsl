#define main LightingMain
#include "Lighting.hlsl"
#undef main
#ifdef SNOW_MELT_REFERENCE
#	include "wetterness_snow_melt_reference.hlsli"
#endif

RWStructuredBuffer<float4> Results : register(u0);

[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint route = id.x % 8;
	SnowCover::CoverageInputs footprint = (SnowCover::CoverageInputs)0;
	footprint.environment = route == 0 ? -2 : (route == 1 ? -0.8 : (route == 3 ? -0.5 : (route == 5 ? 0.5 : 0)));
	footprint.scale = route == 4 ? 0 : 1;
	footprint.position = float3(route == 5 ? 100 : 0, 0, 0);
	footprint.weatherRange = SnowCover::GetWeatherRange(footprint.position, 1000);
	footprint.weatherUpness = 1;
	footprint.foliage = route == 3;
	float3 weights = route == 6 ? float3(1, 0, 0) : (route == 7 ? float3(0, 1, 0) : float3(0, 0, 1));
	if (id.x >= 8)
		weights = float3(0.2, 0.3, 0.5);
	float reuseError = 0;
#ifdef SNOW_MELT_REFERENCE
	float2 wetness = WetternessReference::GetSnowMeltWetness(footprint, weights);
#elif defined(SNOW_MELT_SHARED_FIRE)
	SnowCover::FireMeltSample fireMelt = (SnowCover::FireMeltSample)0;
	if (id.x & 1u)
		SnowCover::GetFireMelt(footprint.position, fireMelt);
	float2 wetness = Wetterness::GetSnowMeltWetness(footprint, weights, fireMelt);
	// Later consumers must see current fires even when melt history exits early.
	reuseError = abs(SnowCover::GetFireMelt(footprint.position, fireMelt) - SnowCover::GetFireMelt(footprint.position));
#else
	float2 wetness = Wetterness::GetSnowMeltWetness(footprint, weights);
#endif
	Results[id.x] = float4(wetness, WetternessDrying::Remaining(24, 24) + reuseError, footprint.weatherRange);
}
