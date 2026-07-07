// Optional VR path: transfer Eye 0's screen-space shadow into Eye 1 and
// skip the Eye 1 raymarch, falling back to the Eye 1 source on disocclusion.

#include "Common/FoveatedMask.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

#ifdef VR

#	if defined(TERRAIN_BLENDING)
Texture2D<float> SrcDepthTexture : register(t0);
#	else
Texture2D<unorm float> SrcDepthTexture : register(t0);
#	endif
Texture2D<unorm float> SrcShadowTexture : register(t1);

RWTexture2D<unorm float> OutShadowTexture : register(u0);

cbuffer StereoSyncCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float2 DispatchBase;
	float2 DispatchExtent;
	float4 FoveatedData0;  // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=enabled
	float4 FoveatedCenterOffset;
};

static const float kDepthAgreeThreshold = 0.05;
static const bool kUseUnjitteredStereoReprojection = true;

float ApplyFoveatedOutputFade(float shadow, float centerWeight)
{
	return lerp(1.0, shadow, centerWeight);
}

[numthreads(8, 8, 1)] void main(uint2 localID : SV_DispatchThreadID) {
	if (any(localID >= uint2(DispatchExtent)))
		return;

	uint2 dtid = localID + uint2(DispatchBase);
	if (any(dtid >= uint2(FrameDim)))
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float centerWeight = 1.0;
	if (FoveatedData0.w > 0.5) {
		const float eyeWidth = max(FrameDim.x * 0.5, 1.0);
		float2 eyePx = float2(dtid);
		if (eyeIndex == 1)
			eyePx.x -= eyeWidth;

		const float2 eyeUV = saturate((eyePx + 0.5) / float2(eyeWidth, FrameDim.y));
		centerWeight = FoveatedComputeCenterBlendWeight(
			eyeUV,
			FoveatedData0.x,
			FoveatedData0.y,
			FoveatedData0.z,
			FoveatedCenterOffset.xy);
		if (centerWeight <= 0.0)
			return;
	}

	float sourceShadow = SrcShadowTexture[dtid];
	float depth = SrcDepthTexture[dtid];

	if (depth < 1e-5 || depth >= 1.0) {
		OutShadowTexture[dtid] = sourceShadow;
		return;
	}

	if (SharedData::GetScreenDepth(depth) < VR_FP_Z) {
		OutShadowTexture[dtid] = sourceShadow;
		return;
	}

	if (eyeIndex == 0) {
		OutShadowTexture[dtid] = ApplyFoveatedOutputFade(sourceShadow, centerWeight);
		return;
	}

	float result = sourceShadow;
	Stereo::StereoBilateralResult reprojection =
		Stereo::ReprojectToOtherEye(uv, depth, eyeIndex, FrameDim, kUseUnjitteredStereoReprojection);
	if (!reprojection.valid) {
		OutShadowTexture[dtid] = ApplyFoveatedOutputFade(result, centerWeight);
		return;
	}

	float otherDepth = SrcDepthTexture[reprojection.otherPx];
	if (otherDepth < 1e-5 ||
		otherDepth >= 1.0 ||
		SharedData::GetScreenDepth(otherDepth) < VR_FP_Z ||
		abs(otherDepth - depth) > kDepthAgreeThreshold) {
		OutShadowTexture[dtid] = ApplyFoveatedOutputFade(result, centerWeight);
		return;
	}

	result = SrcShadowTexture[reprojection.otherPx];
	OutShadowTexture[dtid] = ApplyFoveatedOutputFade(result, centerWeight);
}

#endif  // VR
