#ifndef __VR_STEREO_OPTIMIZATION_CB_HLSLI__
#define __VR_STEREO_OPTIMIZATION_CB_HLSLI__

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

cbuffer VRStereoOptimizationCB : register(b14)
{
	float2 VRStereoRenderDim;
	float2 VRStereoInvRenderDim;
	float2 VRStereoCenterOffsetLeft;
	float2 VRStereoCenterOffsetRight;
	float VRStereoCenterArea;
	float VRStereoDisocclusionThreshold;
	float VRStereoEdgeDepthThreshold;
	float VRStereoEdgeBandPixels;
	float VRStereoCenterProtection;
	float VRStereoCenterFullBlendThreshold;
	float VRStereoNearFieldBlendStart;
	float VRStereoNearFieldBlendEnd;
	uint VRStereoEnableNearFieldFullBlend;
	float3 VRStereoPad0;
};

float2 VRStereoGetCenterOffset(uint eyeIndex)
{
	return eyeIndex == 0 ? VRStereoCenterOffsetLeft : VRStereoCenterOffsetRight;
}

float VRStereoComputeCenterInfluence(float2 monoUV, uint eyeIndex)
{
	const float2 centerUV = float2(0.5, 0.5) + VRStereoGetCenterOffset(eyeIndex);
	const float halfExtent = max(0.001, 0.5 * max(VRStereoCenterArea, 0.001));
	const float2 normalized = (monoUV - centerUV) / halfExtent;
	const float radius = length(normalized);
	return saturate(1.0 - radius);
}

float VRStereoComputeViewDistance(float2 monoUV, float depth, uint eyeIndex)
{
	float4 clipPos = float4(monoUV * float2(2.0, -2.0) - float2(1.0, -1.0), depth, 1.0);
	float4 worldPos = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], clipPos);
	worldPos.xyz /= max(worldPos.w, 1e-5);
	return length(worldPos.xyz);
}

float VRStereoComputeNearFieldBlendFactor(float2 monoUV, float depth, uint eyeIndex)
{
	if (VRStereoEnableNearFieldFullBlend == 0u)
		return 0.0;

	const float viewDistance = VRStereoComputeViewDistance(monoUV, depth, eyeIndex);
	const float range = max(1.0, VRStereoNearFieldBlendEnd - VRStereoNearFieldBlendStart);
	return saturate((VRStereoNearFieldBlendEnd - viewDistance) / range);
}

bool VRStereoIsInsideFrame(float2 uv)
{
	return all(uv >= float2(0.0, 0.0)) && all(uv <= float2(1.0, 1.0));
}

int2 VRStereoUVToPixel(float2 uv)
{
	const float2 clamped = clamp(uv, float2(0.0, 0.0), FrameBuffer::DynamicResolutionParams1.xy);
	const float2 pixel = clamped * VRStereoRenderDim;
	return int2(min(pixel, VRStereoRenderDim - 1.0));
}

#endif  // __VR_STEREO_OPTIMIZATION_CB_HLSLI__
