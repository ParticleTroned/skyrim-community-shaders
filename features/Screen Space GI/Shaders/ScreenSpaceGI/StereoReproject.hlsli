#ifndef SSGI_STEREO_REPROJECT
#define SSGI_STEREO_REPROJECT

// Shared view-independent reproject helpers for SSGI. The same exact-surface test
// gates both the eye-0-only skip and the transfer pass so the hit/miss decisions stay aligned.

#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

float LinearToRawDepth(float d)
{
	return (SharedData::CameraData.x - SharedData::CameraData.w / d) / SharedData::CameraData.z;
}

#if defined(VR) && defined(FRAMEBUFFER)
static const float kGIReprojectDepthAgree = 0.05;

bool GIReprojectsCleanly(float2 uv, float linearDepth, uint eyeIndex, Texture2D<float> depthTex, float2 texScale, out int2 otherPx)
{
	otherPx = int2(0, 0);
	if (linearDepth < FP_Z)
		return false;

	float rawDepth = LinearToRawDepth(linearDepth);
	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, rawDepth, eyeIndex, OUT_FRAME_DIM);
	if (!r.valid)
		return false;

	float otherLinear = depthTex.SampleLevel(samplerPointClamp, r.otherStereoUV * texScale, RES_MIP);
	if (otherLinear < FP_Z)
		return false;

	otherPx = r.otherPx;
	return Stereo::IsReprojectionExact(r, rawDepth, LinearToRawDepth(otherLinear), kGIReprojectDepthAgree);
}
#endif

#endif
