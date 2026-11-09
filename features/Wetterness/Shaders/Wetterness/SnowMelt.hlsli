#ifndef WETTERNESS_SNOW_MELT_HLSLI
#define WETTERNESS_SNOW_MELT_HLSLI

#include "Wetterness/Drying.hlsli"

#if defined(SNOW_COVER) && defined(WETTERNESS) && defined(PSHADER)
namespace Wetterness
{
	float IntegrateMeltInterval(SnowCover::CoverageInputs footprint, float3 before, float3 after,
		float2 fire, float2 coverage, float2 dryingAge)
	{
		float wetness = 0;
		[branch] if (dryingAge.y < 1 && coverage.x > coverage.y)
		{
			// Integrate only still-wet releases; a long wait must not discard the last meltwater.
			float start = saturate((dryingAge.x - 1) / max(0.000001, dryingAge.x - dryingAge.y));
			float previous = start > 0 ? SnowCover::GetCoverage(footprint, lerp(before, after, start), lerp(fire.x, fire.y, start)) : coverage.x;
			float remaining = RemainingDryingFraction(lerp(dryingAge.x, dryingAge.y, start), 1);
			[unroll] for (uint step = 1; step <= 4; ++step)
			{
				float fraction = lerp(start, 1, step * 0.25);
				float snow = step == 4 ? coverage.y : SnowCover::GetCoverage(footprint, lerp(before, after, fraction), lerp(fire.x, fire.y, fraction));
				float nextRemaining = RemainingDryingFraction(lerp(dryingAge.x, dryingAge.y, fraction), 1);
				wetness += max(0, previous - snow) * 0.5 * (remaining + nextRemaining);
				previous = snow;
				remaining = nextRemaining;
			}
		}
		return wetness;
	}

	float2 GetSnowMeltWetness(SnowCover::CoverageInputs footprint, float3 materialWeights)
	{
		float2 wetness = 0;
		uint count = min(SharedData::snowMeltSettings.count, 32u);
		[branch] if (SharedData::snowMeltSettings.enabled && count > 1 && footprint.scale > 0)
		{
			float3 current = float3(SharedData::snowCoverSettings.TimeSnowing,
				SharedData::snowCoverSettings.SnowingDensity, SharedData::snowCoverSettings.SeasonalAltitude);
			float covered = SnowCover::GetCoverage(footprint, current, SnowCover::GetFireMelt(footprint.position));
			[branch] if (covered < 1.0)
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
						// Accumulated drying cannot reverse when later weather becomes colder.
						float retention = lerp(0.25, 1.0, saturate(peak));
						float4 beforeAge = lerp(previousSample.lightDryingAge, previousSample.heavyDryingAge, saturate(peak)) / retention;
						float4 afterAge = lerp(sample.lightDryingAge, sample.heavyDryingAge, saturate(peak)) / retention;
						wetness.x += IntegrateMeltInterval(footprint, previousSample.state.xyz, sample.state.xyz,
							float2(previousFire, fire), float2(previous, coverage), float2(dot(beforeAge.xyz, materialWeights), dot(afterAge.xyz, materialWeights)));
						wetness.y += IntegrateMeltInterval(footprint, previousSample.state.xyz, sample.state.xyz,
							float2(previousFire, fire), float2(previous, coverage), float2(beforeAge.w, afterAge.w));
					}
					previous = coverage;
					previousSample = sample;
					previousFire = fire;
				}
				wetness = saturate(wetness) * (1.0 - covered);
			}
		}
		return wetness;
	}
}
#endif
#endif
