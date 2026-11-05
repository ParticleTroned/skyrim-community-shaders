#ifndef WETTERNESS_SNOW_MELT_HLSLI
#define WETTERNESS_SNOW_MELT_HLSLI

#include "Wetterness/Drying.hlsli"

#if defined(SNOW_COVER) && defined(WETTERNESS) && defined(PSHADER)
namespace Wetterness
{
	float2 GetSnowMeltWetness(SnowCover::CoverageInputs footprint, float3 materialWeights)
	{
		float2 wetness = 0;
		uint count = min(SharedData::snowMeltSettings.count, 32u);
		[branch] if (SharedData::snowMeltSettings.enabled && count > 1 && footprint.scale > 0)
		{
			SharedData::SnowMeltSample previousSample = SharedData::snowMeltSettings.samples[0];
			float previousFire = SnowCover::GetFireMelt(footprint.position, previousSample.fireCount,
				previousSample.fireStrength, previousSample.fireRadiusScale, previousSample.fires);
			float previous = SnowCover::GetCoverage(footprint, previousSample.state.xyz, previousFire);
			float peak = previous;
			[loop] for (uint i = 1; i < count; ++i)
			{
				SharedData::SnowMeltSample sample = SharedData::snowMeltSettings.samples[i];
				float fire = SnowCover::GetFireMelt(footprint.position, sample.fireCount,
					sample.fireStrength, sample.fireRadiusScale, sample.fires);
				float coverage = SnowCover::GetCoverage(footprint, sample.state.xyz, fire);
				peak = previous <= 0 && coverage > 0 ? coverage : max(peak, coverage);
				float melted = max(0.0, previous - coverage);
				[branch] if (melted > 0)
				{
					// Locate this surface's melt within a time skip, including initially saturated snow.
					float low = 0, high = 1;
					float target = 0.5 * (previous + coverage);
					[unroll] for (uint step = 0; step < 6; ++step)
					{
						float middle = 0.5 * (low + high);
						float localSnow = SnowCover::GetCoverage(footprint,
							lerp(previousSample.state.xyz, sample.state.xyz, middle), lerp(previousFire, fire, middle));
						if (localSnow > target)
							low = middle;
						else
							high = middle;
					}
					float released = 0.5 * (low + high);
					float4 lightAge = lerp(previousSample.lightDryingAge, sample.lightDryingAge, released);
					float4 heavyAge = lerp(previousSample.heavyDryingAge, sample.heavyDryingAge, released);
					// Accumulated drying cannot reverse when later weather becomes colder.
					float4 age = lerp(lightAge, heavyAge, saturate(peak)) / lerp(0.25, 1.0, saturate(peak));
					wetness += melted * float2(RemainingDryingFraction(dot(age.xyz, materialWeights), 1),
											RemainingDryingFraction(age.w, 1));
				}
				previous = coverage;
				previousSample = sample;
				previousFire = fire;
			}
			float3 current = float3(SharedData::snowCoverSettings.TimeSnowing,
				SharedData::snowCoverSettings.SnowingDensity, SharedData::snowCoverSettings.SeasonalAltitude);
			float covered = SnowCover::GetCoverage(footprint, current, SnowCover::GetFireMelt(footprint.position));
			wetness = saturate(wetness) * (1.0 - covered);
		}
		return wetness;
	}
}
#endif
#endif
