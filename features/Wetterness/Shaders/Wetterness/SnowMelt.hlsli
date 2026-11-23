#ifndef WETTERNESS_SNOW_MELT_HLSLI
#define WETTERNESS_SNOW_MELT_HLSLI

#include "Wetterness/Drying.hlsli"

#if defined(SNOW_COVER) && defined(WETTERNESS) && defined(PSHADER)
namespace Wetterness
{
	float GetHistoryFireMelt(float3 position, uint sampleIndex)
	{
		float melt = 0;
		uint4 fire = SharedData::snowMeltSettings.fireParameters[sampleIndex];
		uint count = min(fire.x, SnowCover::MaxFireMeltSources);
		[loop] for (uint i = 0; i < count; ++i)
			melt = max(melt, SnowCover::GetFireMeltSphere(position,
								 SharedData::snowMeltSettings.fires[sampleIndex * SnowCover::MaxFireMeltSources + i], asfloat(fire.z)));
		return melt * asfloat(fire.y);
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

	/// Shares the current fire sample with snow rendering without changing history.
	float2 GetSnowMeltWetness(SnowCover::CoverageInputs footprint, float3 materialWeights, inout SnowCover::FireMeltSample fireMelt)
	{
		float2 wetness = 0;
		uint count = min(SharedData::snowMeltSettings.count, 32u);
		[branch] if (SharedData::snowMeltSettings.enabled && count > 1 && footprint.scale > 0)
		{
			// Ignoring fires bounds coverage above every sample, including negative weather accumulation.
			[branch] if (SnowCover::GetCoverage(footprint, SharedData::snowMeltSettings.maximumState.xyz, 0) > 0)
			{
				float3 current = float3(SharedData::snowCoverSettings.TimeSnowing,
					SharedData::snowCoverSettings.SnowingDensity, SharedData::snowCoverSettings.SeasonalAltitude);
				float covered = SnowCover::GetCoverage(footprint, current, SnowCover::GetFireMelt(footprint.position, fireMelt));
				[branch] if (covered < 1.0)
				{
					float3 previousState = SharedData::snowMeltSettings.states[0].xyz;
					float previousFire = GetHistoryFireMelt(footprint.position, 0);
					float previous = SnowCover::GetCoverage(footprint, previousState, previousFire);
					float peak = previous;
					[loop] for (uint i = 1; i < count; ++i)
					{
						float3 state = SharedData::snowMeltSettings.states[i].xyz;
						float fire = 0;
						[branch] if (SharedData::snowMeltSettings.fireParameters[i].x != 0)
						{
							fire = previousFire;
							// Weather samples often retain the same fire footprint for the entire interval.
							[branch] if (!(SharedData::snowMeltSettings.unchangedFireMask & (1u << i)))
								fire = GetHistoryFireMelt(footprint.position, i);
						}
						float coverage = SnowCover::GetCoverage(footprint, state, fire);
						peak = previous <= 0 && coverage > 0 ? coverage : max(peak, coverage);
						float melted = max(0.0, previous - coverage);
						[branch] if (melted > 0)
						{
							// Accumulated drying cannot reverse when later weather becomes colder.
							float retention = lerp(0.25, 1.0, saturate(peak));
							float4 beforeAge = lerp(SharedData::snowMeltSettings.lightDryingAges[i - 1], SharedData::snowMeltSettings.heavyDryingAges[i - 1], saturate(peak)) / retention;
							float4 afterAge = lerp(SharedData::snowMeltSettings.lightDryingAges[i], SharedData::snowMeltSettings.heavyDryingAges[i], saturate(peak)) / retention;
							wetness.x += IntegrateMeltInterval(footprint, previousState, state,
								float2(previousFire, fire), float2(previous, coverage), float2(dot(beforeAge.xyz, materialWeights), dot(afterAge.xyz, materialWeights)));
							wetness.y += IntegrateMeltInterval(footprint, previousState, state,
								float2(previousFire, fire), float2(previous, coverage), float2(beforeAge.w, afterAge.w));
						}
						previous = coverage;
						previousState = state;
						previousFire = fire;
					}
					wetness = saturate(wetness) * (1.0 - covered);
				}
			}
		}
		return wetness;
	}

	float2 GetSnowMeltWetness(SnowCover::CoverageInputs footprint, float3 materialWeights)
	{
		SnowCover::FireMeltSample fireMelt = (SnowCover::FireMeltSample)0;
		return GetSnowMeltWetness(footprint, materialWeights, fireMelt);
	}
}
#endif
#endif
