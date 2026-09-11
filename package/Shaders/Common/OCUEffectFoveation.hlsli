#ifndef OCU_EFFECT_FOVEATION_HLSLI
#define OCU_EFFECT_FOVEATION_HLSLI
#define OCU_EFFECT_FOVEATION_CONSTANTS_VERSION 2

// Compute-only profile; host validates freshness and binds it for one pass.
// Centers use eye-local UVs. Zero strength preserves native sample counts.
cbuffer OCUEffectFoveation : register(b10)
{
	float4 OCUEffectCenters;
	float4 OCUEffectPolicy;  // inner radius, mid radius, strength, eye-tracked
};

float OCUPeripheralEffectWeightForProfile(float2 eyeUV, uint eyeIndex, float4 centers, float4 policy)
{
	if (policy.z <= 0 || policy.x <= 0 || policy.y < policy.x)
		return 0;
	float2 center = eyeIndex == 0 ? centers.xy : centers.zw;
	float distance = length((eyeUV - center) * 2.0);
	// Equal OCU radii describe a hard boundary; keep its inner edge protected.
	float weight = policy.y == policy.x ? (distance > policy.x ? 1.0 : 0.0) :
	                                      smoothstep(policy.x, policy.y, distance);
	return weight * saturate(policy.z);
}

float OCUPeripheralEffectWeight(float2 eyeUV, uint eyeIndex)
{
	return OCUPeripheralEffectWeightForProfile(eyeUV, eyeIndex, OCUEffectCenters, OCUEffectPolicy);
}

#endif
