#include "Common/FoveatedMask.hlsli"

cbuffer FoveatedSpatialCompositeCB : register(b0)
{
	float2 OutputDim;
	float2 InvOutputDim;
	float2 InvPeripherySourceDim;
	float2 PeripherySourceScale;
	float2 PeripherySourceOffset;
	float2 Jitter;
	float2 CenterRectOffset;
	float2 CenterRectDim;
	float2 InvCenterSourceDim;
	float2 CenterOffset;
	float4 Tuning;  // x=centerScale, y=centerFeather, z=centerHorizontalScale
};

Texture2D<float4> PeripheryColor : register(t0);
Texture2D<float4> CenterColor : register(t1);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint2 outputPos = dispatchID.xy;
	if (any(outputPos >= uint2(OutputDim)))
		return;

	float2 outputUV = (float2(outputPos) + 0.5) * InvOutputDim;
	float blendWeight = 0.0;
	float4 centerColor = 0.0.xxxx;
	float2 centerMax = CenterRectOffset + CenterRectDim;
	bool inCenterRect = all(float2(outputPos) >= CenterRectOffset) && all(float2(outputPos) < centerMax);
	if (inCenterRect) {
		blendWeight = FoveatedComputeCenterBlendWeight(outputUV, Tuning.x, Tuning.y, Tuning.z, CenterOffset);
		if (blendWeight > 0.0) {
			float2 centerUV = (float2(outputPos) - CenterRectOffset + 0.5) * InvCenterSourceDim;
			centerColor = CenterColor.SampleLevel(LinearSampler, centerUV, 0.0);
			if (blendWeight >= 1.0) {
				OutputColor[outputPos] = centerColor;
				return;
			}
		}
	}

	float2 sourceRegionMin = PeripherySourceOffset;
	float2 sourceRegionMax = PeripherySourceOffset + PeripherySourceScale;
	float2 halfTexel = InvPeripherySourceDim * 0.5;
	sourceRegionMin = min(sourceRegionMin + halfTexel, sourceRegionMax);
	sourceRegionMax = max(sourceRegionMax - halfTexel, sourceRegionMin);

	float2 peripheryUV = (outputUV * PeripherySourceScale + PeripherySourceOffset) -
		(Jitter * InvPeripherySourceDim);
	peripheryUV = clamp(peripheryUV, sourceRegionMin, sourceRegionMax);
	float4 peripheryColor = PeripheryColor.SampleLevel(LinearSampler, peripheryUV, 0.0);
	OutputColor[outputPos] = blendWeight > 0.0 ? lerp(peripheryColor, centerColor, blendWeight) : peripheryColor;
}
