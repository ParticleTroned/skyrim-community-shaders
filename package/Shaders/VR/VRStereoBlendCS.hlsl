#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"
#include "VR/StereoMode.hlsli"
#include "VR/StereoOptimizationCB.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<uint> ModeTexture : register(t1);

RWTexture2D<float4> MainRW : register(u0);
RWTexture2D<float4> NormalRW : register(u1);
RWTexture2D<float2> MotionRW : register(u2);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	const uint2 pixCoord = dispatchID.xy + uint2(VRStereoDispatchXOffsetPixels, 0u);
	if (any(pixCoord >= uint2(VRStereoRenderDim.xy)))
		return;

	const float2 uvDynamic = (float2(pixCoord) + 0.5) * VRStereoInvRenderDim;
	const float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uvDynamic);
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	if (eyeIndex == 0u)
		return;

	const uint mode = ModeTexture[pixCoord];
	if (mode == VR_STEREO_MODE_NATIVE)
		return;

	const float depth = DepthTexture[pixCoord];
	const float2 monoUV = Stereo::ConvertFromStereoUV(uv, eyeIndex);
	const float3 reprojectedMono = Stereo::ConvertMonoUVToOtherEye(float3(monoUV, depth), eyeIndex);
	if (!VRStereoIsInsideFrame(reprojectedMono.xy))
		return;

	const uint sourceEyeIndex = 1u - eyeIndex;
	float2 sourceStereoUV = Stereo::ConvertToStereoUV(reprojectedMono.xy, sourceEyeIndex);
	sourceStereoUV = VRStereoClampStereoUVToEye(sourceStereoUV, sourceEyeIndex);
	const float2 sourceStereoUVDynamic = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sourceStereoUV);
	const int2 sourceCoord = VRStereoUVToPixel(sourceStereoUVDynamic);

	const float4 sourceMain = MainRW[sourceCoord];
	const float4 sourceNormal = NormalRW[sourceCoord];
	const float2 sourceMotion = MotionRW[sourceCoord];

	if (mode == VR_STEREO_MODE_REPROJECT) {
		MainRW[pixCoord] = sourceMain;
		NormalRW[pixCoord] = sourceNormal;
		MotionRW[pixCoord] = sourceMotion;
		return;
	}

	const float centerInfluence = VRStereoComputeCenterInfluence(monoUV, eyeIndex);
	const float nearBlend = VRStereoComputeNearFieldBlendFactor(monoUV, depth, eyeIndex);
	const float centerBlend = saturate(centerInfluence * VRStereoCenterProtection);
	const float blendBasis = max(centerBlend, nearBlend);
	const float thresholdRange = max(1e-4, 1.0 - VRStereoCenterFullBlendThreshold);
	float blendFactor = saturate((blendBasis - VRStereoCenterFullBlendThreshold) / thresholdRange);
	blendFactor = lerp(0.35, 1.0, blendFactor);

	const float4 currentMain = MainRW[pixCoord];
	const float4 currentNormal = NormalRW[pixCoord];
	const float2 currentMotion = MotionRW[pixCoord];

	MainRW[pixCoord] = lerp(currentMain, sourceMain, blendFactor);
	NormalRW[pixCoord] = lerp(currentNormal, sourceNormal, blendFactor);
	MotionRW[pixCoord] = lerp(currentMotion, sourceMotion, blendFactor);
}
