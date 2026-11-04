#ifndef GRASS_COLLISION_FIELD_HLSLI
#define GRASS_COLLISION_FIELD_HLSLI
#include "GrassCollision/GrassCollisionCommon.hlsli"

namespace GrassCollision
{
	Texture2D<float4> Deformation : register(t100);
	Texture2D<float4> PreviousDeformation : register(t101);
	SamplerState DeformationSampler : register(s15);
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

	void SampleDeformation(float3 root, float4x4 world, float4x4 previousWorld, float distance,
		out float4 current, out float4 previous)
	{
		current = previous = 0;
		if (distance <= 0)
			return;
		float3 currentRoot = mul(world, float4(root, 1)).xyz;
		float3 previousRoot = mul(previousWorld, float4(root, 1)).xyz;
		bool valid;
		float2 uv = GetFieldUV(currentRoot.xy, PosOffset, ArrayOrigin, valid);
		float currentFade = smoothstep(min(distance, FADE_DISTANCE), 0, length(currentRoot));
		if (valid && currentFade > 0 && CollisionStrength > 0) {
			current = Deformation.SampleLevel(DeformationSampler, uv, 0);
			current.w = currentFade;
		}
		uv = GetFieldUV(previousRoot.xy, PreviousPosOffset, PreviousArrayOrigin, valid);
		float previousFade = smoothstep(min(distance, FADE_DISTANCE), 0, length(previousRoot));
		if (valid && previousFade > 0 && PreviousCollisionStrength > 0) {
			previous = PreviousDeformation.SampleLevel(DeformationSampler, uv, 0);
			previous.w = previousFade;
		}
	}
}
#endif
