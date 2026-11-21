#ifndef WETTERNESS_SNOW_MELT_REFERENCE_HLSLI
#define WETTERNESS_SNOW_MELT_REFERENCE_HLSLI

#include "Wetterness/Drying.hlsli"

#if defined(SNOW_COVER) && defined(WETTERNESS) && defined(PSHADER)
namespace WetternessReference
{
	struct SnowMeltSample
	{
		float4 state;
		uint fireCount;
		float fireStrength;
		float fireRadiusScale;
		float4 fires[8];
		float4 lightDryingAge;
		float4 heavyDryingAge;
	};

	SnowMeltSample ReadSample(uint index)
	{
		SnowMeltSample sample;
		sample.state = SharedData::snowMeltSettings.states[index];
		uint4 fire = SharedData::snowMeltSettings.fireParameters[index];
		sample.fireCount = fire.x;
		sample.fireStrength = asfloat(fire.y);
		sample.fireRadiusScale = asfloat(fire.z);
		[unroll] for (uint i = 0; i < 8; ++i)
			sample.fires[i] = SharedData::snowMeltSettings.fires[index * 8 + i];
		sample.lightDryingAge = SharedData::snowMeltSettings.lightDryingAges[index];
		sample.heavyDryingAge = SharedData::snowMeltSettings.heavyDryingAges[index];
		return sample;
	}

	float GetFireMelt(float3 p, uint count, float strength, float radiusScale, float4 spheres[8])
	{
		float melt = 0;
		[loop] for (uint i = 0; i < min(count, 8u); ++i)
			melt = max(melt, SnowCover::GetFireMeltSphere(p, spheres[i], radiusScale));
		return melt * strength;
	}

	float IntegrateMeltInterval(SnowCover::CoverageInputs footprint, float3 before, float3 after,
		float2 fire, float2 coverage, float2 dryingAge)
	{
		float wetness = 0;
		[branch] if (dryingAge.y < 1 && coverage.x > coverage.y)
		{
			// Integrate only still-wet releases; a long wait must not discard the last meltwater.
			float start = saturate((dryingAge.x - 1) / max(0.000001, dryingAge.x - dryingAge.y));
			float previous = start > 0 ? SnowCover::GetCoverage(footprint, lerp(before, after, start), lerp(fire.x, fire.y, start)) : coverage.x;
			float remaining = Wetterness::RemainingDryingFraction(lerp(dryingAge.x, dryingAge.y, start), 1);
			[unroll] for (uint step = 1; step <= 4; ++step)
			{
				float fraction = lerp(start, 1, step * 0.25);
				float snow = step == 4 ? coverage.y : SnowCover::GetCoverage(footprint, lerp(before, after, fraction), lerp(fire.x, fire.y, fraction));
				float nextRemaining = Wetterness::RemainingDryingFraction(lerp(dryingAge.x, dryingAge.y, fraction), 1);
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
				SnowMeltSample previousSample = ReadSample(0);
				float previousFire = GetFireMelt(footprint.position, previousSample.fireCount,
					previousSample.fireStrength, previousSample.fireRadiusScale, previousSample.fires);
				float previous = SnowCover::GetCoverage(footprint, previousSample.state.xyz, previousFire);
				float peak = previous;
				[loop] for (uint i = 1; i < count; ++i)
				{
					SnowMeltSample sample = ReadSample(i);
					float fire = 0;
					[branch] if (sample.fireCount != 0)
					{
						fire = previousFire;
						// Weather samples often retain the same fire footprint for the entire interval.
						[branch] if (!(SharedData::snowMeltSettings.unchangedFireMask & (1u << i)))
							fire = GetFireMelt(footprint.position, sample.fireCount,
								sample.fireStrength, sample.fireRadiusScale, sample.fires);
					}
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
