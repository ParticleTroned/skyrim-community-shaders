#include "Common/FoveatedMask.hlsli"

cbuffer FoveatedCenterBlendCB : register(b0)
{
	float2 InvOutputDim;
	float CenterScale;
	float CenterFeather;
	float2 CenterOffset;
	float2 OutputOffset;
	float2 DispatchDim;
	float2 SourceOffset;
	float2 InvSourceDim;
	float CenterHorizontalScale;
	uint TargetOffsetX;
	uint CharacterSelectionMode;
	uint3 Padding;
};

Texture2D<float4> CenterColor : register(t0);
Texture2D<float4> BaselineCenterColor : register(t1);
Texture2D<float> CharacterMask : register(t2);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 localPos = dispatchID.xy;
	if (any(localPos >= uint2(DispatchDim)))
		return;

	uint2 outputPos = localPos + uint2(OutputOffset + 0.5);
	uint2 targetPos = outputPos + uint2(TargetOffsetX, 0);
	float2 outputUV = (float2(outputPos) + 0.5) * InvOutputDim;
	float blendWeight = FoveatedComputeCenterBlendWeight(outputUV, CenterScale, CenterFeather, CenterHorizontalScale, CenterOffset);
	if (blendWeight <= 0.0)
		return;

	float2 centerUV = (float2(localPos) + SourceOffset + 0.5) * InvSourceDim;
	float4 centerColor = CenterColor.SampleLevel(LinearSampler, centerUV, 0);
	if (CharacterSelectionMode != 0) {
		// Feature 18 consumes the authored strength. This pass only gates any
		// provider leakage outside the nonzero character support.
		float characterWeight = saturate(
			CharacterMask.SampleLevel(LinearSampler, centerUV, 0) * 255.0);
		float4 baselineColor = BaselineCenterColor.SampleLevel(LinearSampler, centerUV, 0);
		centerColor = lerp(baselineColor, centerColor, characterWeight);
	}

	if (blendWeight >= 1.0) {
		OutputColor[targetPos] = centerColor;
	} else {
		float4 baseColor = OutputColor[targetPos];
		OutputColor[targetPos] = lerp(baseColor, centerColor, blendWeight);
	}
}
