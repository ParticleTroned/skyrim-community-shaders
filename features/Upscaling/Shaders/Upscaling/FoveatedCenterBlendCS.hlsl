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
	uint FinalLdrColorMode;
	uint FullImage;
	uint Padding;
	float4 CharacterMaskBounds;
};

Texture2D<float4> CenterColor : register(t0);
Texture2D<float4> BaselineCenterColor : register(t1);
Texture2D<float> CharacterMask : register(t2);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputColor : register(u0);

float4 PrepareFinalLdrModelColor(float4 modelColor, float4 originalColor)
{
	// Final LDR has already passed scene post-processing. For UNORM targets,
	// constrain the model before character selection or feathering; clamping
	// only the final UAV store would amplify out-of-range highlights.
	float3 color = originalColor.rgb;
	if (all(isfinite(modelColor.rgb)))
		color = FinalLdrColorMode == 2 ? saturate(modelColor.rgb) : modelColor.rgb;
	return float4(color, originalColor.a);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 localPos = dispatchID.xy;
	if (any(localPos >= uint2(DispatchDim)))
		return;

	uint2 outputPos = localPos + uint2(OutputOffset + 0.5);
	uint2 targetPos = outputPos + uint2(TargetOffsetX, 0);
	float2 outputUV = (float2(outputPos) + 0.5) * InvOutputDim;
	float blendWeight = FullImage != 0 ? 1.0 : FoveatedComputeCenterBlendWeight(outputUV, CenterScale, CenterFeather, CenterHorizontalScale, CenterOffset);
	if (blendWeight <= 0.0)
		return;

	float4 originalColor = 0.0;
	if (FinalLdrColorMode != 0)
		originalColor = OutputColor[targetPos];

	float2 centerUV = (float2(localPos) + SourceOffset + 0.5) * InvSourceDim;
	float4 centerColor = 0.0;
	if (CharacterSelectionMode != 0) {
		// Character inputs share this integer texel grid. Filtering can pull
		// unproduced ROI neighbours into an otherwise valid selected texel.
		int3 sourcePos = int3(localPos + uint2(SourceOffset + 0.5), 0);
		float characterWeight = 0.0;
		[branch] if (all(centerUV >= CharacterMaskBounds.xy) && all(centerUV < CharacterMaskBounds.zw))
			characterWeight = saturate(CharacterMask.Load(sourcePos));
		float4 baselineColor = BaselineCenterColor.Load(sourcePos);
		centerColor = baselineColor;
		if (characterWeight > 0.0) {
			float4 neuralColor = CenterColor.Load(sourcePos);
			if (FinalLdrColorMode != 0)
				neuralColor = PrepareFinalLdrModelColor(neuralColor, originalColor);
			centerColor = lerp(baselineColor, neuralColor, characterWeight);
		}
	} else {
		centerColor = CenterColor.SampleLevel(LinearSampler, centerUV, 0);
		if (FinalLdrColorMode != 0)
			centerColor = PrepareFinalLdrModelColor(centerColor, originalColor);
	}

	if (FinalLdrColorMode != 0) {
		float3 color = blendWeight >= 1.0 ? centerColor.rgb : lerp(originalColor.rgb, centerColor.rgb, blendWeight);
		OutputColor[targetPos] = float4(color, originalColor.a);
		return;
	}

	if (blendWeight >= 1.0) {
		OutputColor[targetPos] = centerColor;
	} else {
		float4 baseColor = OutputColor[targetPos];
		OutputColor[targetPos] = lerp(baseColor, centerColor, blendWeight);
	}
}
