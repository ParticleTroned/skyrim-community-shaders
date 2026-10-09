#include "Common/FoveatedMask.hlsli"
#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"

Texture2D<float4> SourceColor : register(t0);
Texture2D<float4> ProxyColor : register(t1);
Texture2D<float4> ProxyNeural : register(t2);
RWTexture2D<float4> Result : register(u0);

bool ReadResidual(int2 coordinate, out float3 residual)
{
	coordinate = clamp(coordinate, int2(ModelRegionOffset), int2(ModelRegionOffset + ModelRegionSize - 1u));
	float3 original = ProxyColor.Load(int3(coordinate, 0)).rgb;
	float3 neural = ProxyNeural.Load(int3(coordinate, 0)).rgb;
	residual = neural - original;
	return all(isfinite(original)) && all(isfinite(neural)) && all(isfinite(residual));
}

float CentralWeight(float2 uv)
{
	const float distance = FoveatedComputeMaskDistance(uv, CentralScale, CentralHorizontalScale, CentralOffset);
	if (distance <= 1.0)
		return 1.0;
	if (CentralFeather == 0.0)
		return 0.0;
	const float2 radii = FoveatedComputeMaskRadii(CentralScale, CentralHorizontalScale);
	const float2 normalized = abs((uv - FoveatedComputeCenterUV(CentralOffset)) / radii);
	// The mask gradient converts radial distance into output pixels near the boundary.
	const float2 gradient = normalized * normalized * normalized / (radii * FinalOutputSize * distance * distance * distance);
	const float edgeDistance = (distance - 1.0) / length(gradient);
	return 1.0 - smoothstep(0.0, CentralFeather, edgeDistance);
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= SourceRegionSize))
		return;
	uint2 pixel = SourceRegionOffset + id.xy;
	float4 source = SourceColor.Load(int3(pixel, 0));
	float weight = 1.0;
	if (CentralActive != 0u) {
		const float2 uv = (float2(pixel) + CropOrigin + 0.5) / FullEyeSize;
		weight = CentralWeight(uv);
	}
	if (weight <= 0.0) {
		Result[pixel] = source;
		return;
	}
	float2 position = (float2(pixel) + 0.5) * float2(ModelSize) / float2(SourceSize) - 0.5;
	int2 base = int2(floor(position));
	float2 phase = frac(position);
	float3 r00, r10, r01, r11;
	bool valid = ReadResidual(base, r00);
	valid = ReadResidual(base + int2(1, 0), r10) && valid;
	valid = ReadResidual(base + int2(0, 1), r01) && valid;
	valid = ReadResidual(base + int2(1, 1), r11) && valid;
	float3 residual = lerp(lerp(r00, r10, phase.x), lerp(r01, r11, phase.x), phase.y);
	// Reconstruct only the matched change; the original image retains its fine detail.
	precise float3 candidate = source.rgb + residual * weight;
	valid = valid && all(isfinite(candidate));
	if (OutputFormat == 1u)
		valid = valid && all(abs(candidate) <= 65504.0);
	else if (OutputFormat == 2u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= float3(65024.0, 65024.0, 64512.0));
	else if (OutputFormat == 3u)
		valid = valid && all(candidate >= 0.0) && all(candidate <= 1.0);
	Result[pixel] = float4(valid ? candidate : source.rgb, source.a);
}
