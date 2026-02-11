#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float UnderwaterMask : SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float> UnderwaterMask : register(t0);

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float useWideKernel;
	float pad0;
};

float2 ClampToDynamicResBounds(float2 uv, float2 screenPos)
{
	float2 minValue = float2(0.0, 0.0);
	float2 maxValue = FrameBuffer::DynamicResolutionParams1.xy;

#	if defined(VR)
	bool isRight = screenPos.x >= 0.5;
	float minFactor = isRight ? 1.0 : 0.0;
	float maxFactor = isRight ? 2.0 : 1.0;
	minValue.x = 0.5 * (FrameBuffer::DynamicResolutionParams2.z * minFactor);
	maxValue.x = 0.5 * (FrameBuffer::DynamicResolutionParams2.z * maxFactor);
#	endif

	return clamp(uv, minValue, maxValue);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within bounds (VR: preserve per-eye bounds).
	uv = ClampToDynamicResBounds(uv, input.TexCoord);

	// Upscale using linear sampling with jitter-corrected coordinates
	psout.UnderwaterMask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif
