#ifndef GRASS_COLLISION_FIELD_HLSLI
#define GRASS_COLLISION_FIELD_HLSLI
#include "GrassCollision/GrassCollisionCommon.hlsli"
#include "GrassCollision/GrassCollisionContact.hlsli"

namespace GrassCollision
{
	Texture2D<float4> Deformation : register(t100);
	Texture2D<float4> PreviousDeformation : register(t101);
	Texture2D<float4> ContactMotion : register(t102);
	Texture2D<float4> PreviousContactMotion : register(t103);
	static const uint TEXTURE_SIZE = 1024;
	static const float WORLD_SIZE = 8192.0;
	static const float FADE_DISTANCE = WORLD_SIZE * 0.5;

	float2 GetFieldUV(float2 worldPosition, float2 positionOffset, uint2 arrayOrigin, out bool valid)
	{
		float2 logicalUV = (worldPosition - positionOffset) / WORLD_SIZE + 0.5;
		valid = all(isfinite(logicalUV)) && all(logicalUV >= 0) && all(logicalUV <= 1);
		// Clamp at logical edge texel centers so filtering cannot read the opposite ring edge.
		logicalUV = clamp(logicalUV, 0.5 / TEXTURE_SIZE, 1.0 - 0.5 / TEXTURE_SIZE);
		return frac(logicalUV + float2(arrayOrigin) / TEXTURE_SIZE);
	}

	float4 SampleContactLayer(Texture2D<float4> field, Texture2D<float4> motion, float2 uv,
		float rootHeight, float bladeHeight, float fade)
	{
		float2 texel = uv * TEXTURE_SIZE - 0.5;
		int2 origin = int2(floor(texel));
		float2 fraction = frac(texel);
		float3 result = 0;
		[unroll] for (uint y = 0; y < 2; ++y)
			[unroll] for (uint x = 0; x < 2; ++x)
		{
			uint2 cell = (uint2(origin + int2(x, y)) + TEXTURE_SIZE) % TEXTURE_SIZE;
			float4 state = field.Load(int3(cell, 0));
			[branch] if (state.w <= 0 || !all(isfinite(state))) continue;
			float2 bounds = motion.Load(int3(cell, 0)).zw;
			float2 weight = lerp(1 - fraction, fraction, float2(x, y));
			// Reject unrelated heights before filtering, so adjacent floors cannot invent a contact.
			result += state.xyz * (weight.x * weight.y * GrassCollisionContact::BladeWeight(bounds, rootHeight, bladeHeight));
		}
		return float4(result, fade);
	}

	void SampleDeformation(float3 root, float3 currentReach, float3 previousReach, float4x4 world, float4x4 previousWorld, float distance,
		out float4 current, out float4 previous)
	{
		current = previous = 0;
		if (distance <= 0)
			return;
		float3 currentRoot = mul(world, float4(root, 1)).xyz;
		float3 previousRoot = mul(previousWorld, float4(root, 1)).xyz;
		float currentHeight = max(mul((float3x3)world, currentReach).z, 0);
		float previousHeight = max(mul((float3x3)previousWorld, previousReach).z, 0);
		bool valid;
		float2 uv = GetFieldUV(currentRoot.xy, PosOffset, ArrayOrigin, valid);
		float currentFade = smoothstep(min(distance, FADE_DISTANCE), 0, length(currentRoot));
		if (valid && currentFade > 0 && CollisionStrength > 0) {
			current = SampleContactLayer(Deformation, ContactMotion, uv, currentRoot.z + CurrentHeightOffset, currentHeight, currentFade);
		}
		uv = GetFieldUV(previousRoot.xy, PreviousPosOffset, PreviousArrayOrigin, valid);
		float previousFade = smoothstep(min(distance, FADE_DISTANCE), 0, length(previousRoot));
		if (valid && previousFade > 0 && PreviousCollisionStrength > 0) {
			previous = SampleContactLayer(PreviousDeformation, PreviousContactMotion, uv, previousRoot.z + PreviousHeightOffset, previousHeight, previousFade);
		}
	}
}
#endif
