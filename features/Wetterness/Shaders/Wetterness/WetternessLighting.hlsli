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

	struct RainDropCellContext
	{
		int2 gridCurr;
		float2 cellOffset;
		float2 gridUV;
		float tOffset;
		float worldPhase;
		float uintToFloat;
		float raindropChance;
	};

	uint3 GetRainDropHash(RainDropCellContext cell, float phaseTime, out float residual)
	{
		residual = phaseTime + cell.tOffset + cell.worldPhase;
		uint timestep = uint(residual);
		residual -= timestep;
		return Random::pcg3d(uint3(asuint(cell.gridCurr), timestep));
	}

	void AccumulateRainSplashWetness(
		inout float wetness,
		RainDropCellContext cell,
		float phaseTime,
		float splashMaxRadiusSqr)
	{
		float residual;
		uint3 hash = GetRainDropHash(cell, phaseTime, residual);
		float3 floatHash = float3(hash) * cell.uintToFloat;

		if (floatHash.z < cell.raindropChance) {
			float2 vec2Centre = cell.cellOffset + floatHash.xy - cell.gridUV;
			float distSqr = dot(vec2Centre, vec2Centre);
			if (distSqr < splashMaxRadiusSqr) {
				float dropRadius = lerp(SharedData::wetternessSettings.SplashesMinRadius,
				                      SharedData::wetternessSettings.SplashesMaxRadius,
				                      float(Random::iqint3(hash.yz)) * cell.uintToFloat);
				if (distSqr < dropRadius * dropRadius) {
					wetness = max(wetness, RainFade(residual));
				}
			}
		}
	}

	void AccumulateRainRippleNormal(
		inout float3 rippleNormal,
		RainDropCellContext cell,
		float phaseTime,
		float lifetimeRcp,
		float rippleMaxRadiusSqr,
		float rippleBreadthRcp,
		float rippleStrength)
	{
		float residual;
		uint3 hash = GetRainDropHash(cell, phaseTime, residual);
		float3 floatHash = float3(hash) * cell.uintToFloat;

		if (floatHash.z < cell.raindropChance) {
			float2 vec2Centre = cell.cellOffset + floatHash.xy - cell.gridUV;
			float distSqr = dot(vec2Centre, vec2Centre);
			float rippleT = residual * lifetimeRcp;

			if (rippleT < 1.0 && distSqr < rippleMaxRadiusSqr) {
				// Vary ripple size using high-quality random hash.
				uint sizeHash = Random::iqint3(hash.xy);
				float sizeVariation = lerp(0.7, 1.3, float(sizeHash) * cell.uintToFloat);

				float rippleRadius = SharedData::wetternessSettings.RippleRadius * sizeVariation;
				float rippleR = lerp(0.0, rippleRadius, rippleT);
				float rippleInnerRadius = rippleR - SharedData::wetternessSettings.RippleBreadth;

				bool insideOuterRadius = distSqr < rippleR * rippleR;
				bool outsideInnerRadius = rippleInnerRadius <= 0.0 || distSqr > rippleInnerRadius * rippleInnerRadius;
				if (insideOuterRadius && outsideInnerRadius) {
					float bandLerp = (sqrt(distSqr) - rippleInnerRadius) * rippleBreadthRcp;
					if (distSqr > 0.0 && bandLerp > 0.0 && bandLerp < 1.0) {
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
		bool hasEffects = enableSplashes || enableRipples;

		// Return before timing setup when the effect is disabled or runtime settings make
		// every cell contribution impossible.
		if (!hasEffects || raindropChance <= 0.0 || intervalRcp <= 0.0 || lifetimeRcp <= 0.0) {
			return float4(float3(0, 0, 1), 0.0);
		}

		float splashMaxRadius = max(SharedData::wetternessSettings.SplashesMinRadius, SharedData::wetternessSettings.SplashesMaxRadius);
		float splashMaxRadiusSqr = splashMaxRadius * splashMaxRadius;
		float rippleMaxRadius = max(SharedData::wetternessSettings.RippleRadius, 0.0) * 1.3;
		float rippleMaxRadiusSqr = rippleMaxRadius * rippleMaxRadius;
		float splashTime = 0.0;
		if (enableSplashes) {
			float splashLifetime = max(SharedData::wetternessSettings.SplashesLifetime, 1e-3);
			splashTime = t * intervalRcp / splashLifetime;
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

				RainDropCellContext cell;
				cell.gridCurr = grid + int2(i, j);
				cell.cellOffset = float2(i, j);
				cell.gridUV = gridUV;
				cell.tOffset = float(Random::iqint3(cell.gridCurr)) * uintToFloat;
				cell.worldPhase = worldPhase;
				cell.uintToFloat = uintToFloat;
				cell.raindropChance = raindropChance;

				// Calculate splashes-only path
				if (splashesOnly) {
					AccumulateRainSplashWetness(wetness, cell, splashTime, splashMaxRadiusSqr);
					continue;
				}

				// Calculate ripples-only path
				if (ripplesOnly) {
					AccumulateRainRippleNormal(rippleNormal, cell, rippleTime, lifetimeRcp, rippleMaxRadiusSqr, rippleBreadthRcp, rippleStrength);
					continue;
				}

				// Calculate combined splash+ripple path
				AccumulateRainSplashWetness(wetness, cell, splashTime, splashMaxRadiusSqr);
				AccumulateRainRippleNormal(rippleNormal, cell, rippleTime, lifetimeRcp, rippleMaxRadiusSqr, rippleBreadthRcp, rippleStrength);
			}
		}

		return float4(rippleNormal, wetness * splashStrength);
	}
}

#endif
