#include "Common/Color.hlsli"

#ifndef GUIDED_NR_GUIDANCE_STRENGTH
#	define GUIDED_NR_GUIDANCE_STRENGTH 16.0
#endif

float GuideSimilarity(float3 reference, float3 proxy)
{
	const float3 difference = reference - proxy;
	return rcp(1.0 + GUIDED_NR_GUIDANCE_STRENGTH * Color::RGBToLuminance(difference * difference));
}

bool GuidedResidual(float2 position, uint4 bounds, float3 reference, out float3 residual)
{
	const int2 base = int2(floor(position));
	const float2 phase = frac(position);
	float3 r00, r10, r01, r11, p00, p10, p01, p11;
	bool valid = ReadResidual(base, bounds, r00, p00);
	valid = ReadResidual(base + int2(1, 0), bounds, r10, p10) && valid;
	valid = ReadResidual(base + int2(0, 1), bounds, r01, p01) && valid;
	valid = ReadResidual(base + int2(1, 1), bounds, r11, p11) && valid;
	residual = lerp(lerp(r00, r10, phase.x), lerp(r01, r11, phase.x), phase.y);
	if (!valid || !all(isfinite(reference)))
		return valid;
	// One shared magnitude keeps signed HDR distances bounded and invariant to common exposure changes.
	const float3 magnitude = max(abs(reference), max(max(abs(p00), abs(p10)), max(abs(p01), abs(p11))));
	const float scale = max(magnitude.x, max(magnitude.y, magnitude.z));
	if (scale <= 1e-6)
		return true;
	const float3 normalized = reference / scale;
	const float4 spatial = float4((1.0 - phase.x) * (1.0 - phase.y), phase.x * (1.0 - phase.y), (1.0 - phase.x) * phase.y, phase.x * phase.y);
	const float4 weights = spatial * float4(GuideSimilarity(normalized, p00 / scale), GuideSimilarity(normalized, p10 / scale),
										 GuideSimilarity(normalized, p01 / scale), GuideSimilarity(normalized, p11 / scale));
	const float normalization = dot(weights, 1.0);
	if (!all(isfinite(weights)) || any(weights < 0.0) || !isfinite(normalization) || normalization <= 1e-6)
		return true;
	const float3 guided = (weights.x * r00 + weights.y * r10 + weights.z * r01 + weights.w * r11) / normalization;
	// Poor optional guidance retains the valid bilinear estimate, never a zero neural edit.
	if (all(isfinite(guided)))
		residual = guided;
	return true;
}
