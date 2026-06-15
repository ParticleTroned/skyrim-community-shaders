#ifndef WETTERNESS_LIGHTING_HLSLI
#define WETTERNESS_LIGHTING_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

namespace Wetterness
{
	Texture2D<float4> TexPrecipOcclusion : register(t70);

	// https://github.com/BelmuTM/Noble/blob/master/LICENSE.txt

	float SmoothstepDeriv(float x)
	{
		return 6.0 * x * (1. - x);
	}

	float RainFade(float normalised_t)
	{
		const float rain_stay = .5;

		if (normalised_t < rain_stay)
			return 1.0;

		float val = lerp(1.0, 0.0, (normalised_t - rain_stay) / (1.0 - rain_stay));
		return val * val;
	}

	// https://blog.selfshadow.com/publications/blending-in-detail/
	// geometric normal s, a base normal t and a secondary (or detail) normal u
	float3 ReorientNormal(float3 u, float3 t, float3 s)
	{
		// Build the shortest-arc quaternion
		float4 q = float4(cross(s, t), dot(s, t) + 1) / sqrt(2 * (dot(s, t) + 1));

		// Rotate the normal
		return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
	}

	// for when s = (0,0,1)
	float3 ReorientNormal(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);

		return n1 * dot(n1, n2) / n1.z - n2;
	}

	// xyz - ripple normal, w - splotches
	float4 GetRainDrops(float3 worldPos, float t, float3 normal, float rippleStrengthModifier = 1.0, float2 flowOffset = float2(0.0, 0.0))
	{
		// Apply flow offset to world position for flow-aware ripple positioning
		worldPos.xy += flowOffset;

		// Precompute constants
		float uintToFloat = rcp(4294967295.0);
		float rippleBreadthRcp = rcp(max(SharedData::wetternessSettings.RippleBreadth, 1e-3));
		float intervalRcp = SharedData::wetternessSettings.RaindropIntervalRcp;
		float lifetimeRcp = SharedData::wetternessSettings.RippleLifetimeRcp;
		float raindropChance = saturate(SharedData::wetternessSettings.RaindropChance);
		bool enableSplashes = SharedData::wetternessSettings.EnableSplashes;
		bool enableRipples = SharedData::wetternessSettings.EnableRipples;
		float splashMaxRadius = max(SharedData::wetternessSettings.SplashesMinRadius, SharedData::wetternessSettings.SplashesMaxRadius);
		float splashMaxRadiusSqr = splashMaxRadius * splashMaxRadius;
		float rippleMaxRadius = max(SharedData::wetternessSettings.RippleRadius, 0.0) * 1.3;
		float rippleMaxRadiusSqr = rippleMaxRadius * rippleMaxRadius;
		float splashTime = 0.0;
		if (enableSplashes) {
			splashTime = t * intervalRcp / SharedData::wetternessSettings.SplashesLifetime;
		}
		float rippleTime = t * intervalRcp;
		float worldPhase = worldPos.z * 0.001;
		float splashStrength = SharedData::wetternessSettings.SplashesStrength;
		float rippleStrength = SharedData::wetternessSettings.RippleStrength * rippleStrengthModifier;
		bool splashesOnly = enableSplashes && !enableRipples;
		bool ripplesOnly = enableRipples && !enableSplashes;
		float activeMaxRadiusSqr = max(enableSplashes ? splashMaxRadiusSqr : 0.0, enableRipples ? rippleMaxRadiusSqr : 0.0);

		// Calculate grid coordinates
		float2 gridUV = worldPos.xy * SharedData::wetternessSettings.RaindropGridSizeRcp + normal.xy;
		int2 grid = floor(gridUV);
		gridUV -= grid;

		// Initialize output values
		float3 rippleNormal = float3(0, 0, 1);
		float wetness = 0.0;

		// Early exit if no effects enabled
		bool hasEffects = enableSplashes || enableRipples;
		if (!hasEffects || raindropChance <= 0.0 || intervalRcp <= 0.0 || lifetimeRcp <= 0.0) {
			return float4(rippleNormal, wetness * splashStrength);
		}

		// Process surrounding grid cells
		for (int i = -1; i <= 1; i++) {
			for (int j = -1; j <= 1; j++) {
				// Conservative exact reject: if the closest point in this cell is still outside
				// every active effect radius, the cell cannot contribute.
				float2 cellMin = float2(i, j) - gridUV;
				float2 cellMax = cellMin + 1.0.xx;
				float2 cellClosest = clamp(0.0.xx, cellMin, cellMax);
				float minCellDistSqr = dot(cellClosest, cellClosest);
				if (minCellDistSqr >= activeMaxRadiusSqr) {
					continue;
				}

				int2 gridCurr = grid + int2(i, j);
				float tOffset = float(Random::iqint3(gridCurr)) * uintToFloat;

				// Calculate splashes-only path
				if (splashesOnly) {
					float residual = splashTime + tOffset + worldPhase;
					uint timestep = uint(residual);
					residual -= timestep;

					uint3 hash = Random::pcg3d(uint3(asuint(gridCurr), timestep));
					float3 floatHash = float3(hash) * uintToFloat;

					if (floatHash.z < raindropChance) {
						float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
						float distSqr = dot(vec2Centre, vec2Centre);
						if (distSqr < splashMaxRadiusSqr) {
							float dropRadius = lerp(SharedData::wetternessSettings.SplashesMinRadius,
							                      SharedData::wetternessSettings.SplashesMaxRadius,
							                      float(Random::iqint3(hash.yz)) * uintToFloat);
							if (distSqr < dropRadius * dropRadius) {
								wetness = max(wetness, RainFade(residual));
							}
						}
					}
					continue;
				}

				// Calculate ripples-only path
				if (ripplesOnly) {
					float residual = rippleTime + tOffset + worldPhase;
					uint timestep = uint(residual);
					residual -= timestep;

					uint3 hash = Random::pcg3d(uint3(asuint(gridCurr), timestep));
					float3 floatHash = float3(hash) * uintToFloat;

					if (floatHash.z < raindropChance) {
						float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
						float distSqr = dot(vec2Centre, vec2Centre);
						float rippleT = residual * lifetimeRcp;

						if (rippleT < 1.0 && distSqr < rippleMaxRadiusSqr) {
							// Vary ripple size using high-quality random hash
							uint sizeHash = Random::iqint3(hash.xy);
							float sizeVariation = lerp(0.7, 1.3, float(sizeHash) * uintToFloat);

							float rippleRadius = SharedData::wetternessSettings.RippleRadius * sizeVariation;
							float rippleR = lerp(0.0, rippleRadius, rippleT);
							float rippleInnerRadius = rippleR - SharedData::wetternessSettings.RippleBreadth;

							bool insideOuterRadius = distSqr < rippleR * rippleR;
							bool outsideInnerRadius = rippleInnerRadius <= 0.0 || distSqr > rippleInnerRadius * rippleInnerRadius;
							if (insideOuterRadius && outsideInnerRadius) {
								float bandLerp = (sqrt(distSqr) - rippleInnerRadius) * rippleBreadthRcp;
								if (bandLerp > 0.0 && bandLerp < 1.0) {
									float deriv = (bandLerp < 0.5 ? SmoothstepDeriv(bandLerp * 2.0) : -SmoothstepDeriv(2.0 - bandLerp * 2.0)) *
									              lerp(rippleStrength, 0.0, rippleT * rippleT);

									float3 grad = float3(normalize(vec2Centre), -deriv);
									float3 bitangent = float3(-grad.y, grad.x, 0.0);
									float3 rippleSampleNormal = normalize(cross(grad, bitangent));

									rippleNormal = ReorientNormal(rippleSampleNormal, rippleNormal);
								}
							}
						}
					}
					continue;
				}

				// Calculate combined splash+ripple path
				float splashResidual = splashTime + tOffset + worldPhase;
				uint splashTimestep = uint(splashResidual);
				splashResidual -= splashTimestep;
				uint3 splashHash = Random::pcg3d(uint3(asuint(gridCurr), splashTimestep));
				float3 splashFloatHash = float3(splashHash) * uintToFloat;
				if (splashFloatHash.z < raindropChance) {
					float2 vec2Centre = int2(i, j) + splashFloatHash.xy - gridUV;
					float distSqr = dot(vec2Centre, vec2Centre);
					if (distSqr < splashMaxRadiusSqr) {
						float dropRadius = lerp(SharedData::wetternessSettings.SplashesMinRadius,
						                      SharedData::wetternessSettings.SplashesMaxRadius,
						                      float(Random::iqint3(splashHash.yz)) * uintToFloat);
						if (distSqr < dropRadius * dropRadius) {
							wetness = max(wetness, RainFade(splashResidual));
						}
					}
				}

				float rippleResidual = rippleTime + tOffset + worldPhase;
				uint rippleTimestep = uint(rippleResidual);
				rippleResidual -= rippleTimestep;
				uint3 rippleHash = Random::pcg3d(uint3(asuint(gridCurr), rippleTimestep));
				float3 rippleFloatHash = float3(rippleHash) * uintToFloat;
				if (rippleFloatHash.z < raindropChance) {
					float2 vec2Centre = int2(i, j) + rippleFloatHash.xy - gridUV;
					float distSqr = dot(vec2Centre, vec2Centre);
					float rippleT = rippleResidual * lifetimeRcp;

					if (rippleT < 1.0 && distSqr < rippleMaxRadiusSqr) {
						uint sizeHash = Random::iqint3(rippleHash.xy);
						float sizeVariation = lerp(0.7, 1.3, float(sizeHash) * uintToFloat);
						float rippleRadius = SharedData::wetternessSettings.RippleRadius * sizeVariation;
						float rippleR = lerp(0.0, rippleRadius, rippleT);
						float rippleInnerRadius = rippleR - SharedData::wetternessSettings.RippleBreadth;

						bool insideOuterRadius = distSqr < rippleR * rippleR;
						bool outsideInnerRadius = rippleInnerRadius <= 0.0 || distSqr > rippleInnerRadius * rippleInnerRadius;
						if (insideOuterRadius && outsideInnerRadius) {
							float bandLerp = (sqrt(distSqr) - rippleInnerRadius) * rippleBreadthRcp;
							if (bandLerp > 0.0 && bandLerp < 1.0) {
								float deriv = (bandLerp < 0.5 ? SmoothstepDeriv(bandLerp * 2.0) : -SmoothstepDeriv(2.0 - bandLerp * 2.0)) *
								              lerp(rippleStrength, 0.0, rippleT * rippleT);

								float3 grad = float3(normalize(vec2Centre), -deriv);
								float3 bitangent = float3(-grad.y, grad.x, 0.0);
								float3 rippleSampleNormal = normalize(cross(grad, bitangent));

								rippleNormal = ReorientNormal(rippleSampleNormal, rippleNormal);
							}
						}
					}
				}
			}
		}

		return float4(rippleNormal, wetness * splashStrength);
	}
}

#endif
