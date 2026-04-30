// Bilateral stereo blend for SSGI output. AO-only builds skip all IL resources.

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float> srcDepth : register(t0);
Texture2D<float> srcAo : register(t1);
#ifdef GI
Texture2D<float4> srcIlY : register(t2);
Texture2D<float2> srcIlCoCg : register(t3);
#endif

RWTexture2D<float> outAo : register(u0);
#ifdef GI
RWTexture2D<float4> outIlY : register(u1);
RWTexture2D<float2> outIlCoCg : register(u2);
#endif

static const float kDepthSigma = 0.01;
static const float kMaxBlend = 0.5;
static const float kBackCheckThreshold = 8.0;

void StoreSource(uint2 dstPx, uint2 srcPx)
{
	outAo[dstPx] = srcAo[srcPx];
#ifdef GI
	outIlY[dstPx] = srcIlY[srcPx];
	outIlCoCg[dstPx] = srcIlCoCg[srcPx];
#endif
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID)
{
	const float2 outFrameDim = OUT_FRAME_DIM;

#ifdef CENTER_DISPATCH
	if (any(dtid >= uint2((uint)CenterDispatchSizeX, (uint)CenterDispatchSizeY)))
		return;
	uint2 px = dtid + uint2((uint)CenterDispatchOffsetX, (uint)CenterDispatchOffsetY);
#else
	uint2 px = dtid;
#endif

	if (any(px >= uint2(outFrameDim)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;
	float2 uv = (px + 0.5) / outFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

#ifdef VR
	if (StereoEnabled == 0) {
		StoreSource(px, px);
		return;
	}
#endif

	float depth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, RES_MIP);
	if (depth < FP_Z) {
		StoreSource(px, px);
		return;
	}

	float rawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / depth) / SharedData::CameraData.z;
	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, rawDepth, eyeIndex, outFrameDim);
	if (!r.valid) {
		StoreSource(px, px);
		return;
	}

	float otherLinearDepth = srcDepth.SampleLevel(samplerPointClamp, r.otherStereoUV * frameScale, RES_MIP);
	if (otherLinearDepth < FP_Z) {
		StoreSource(px, px);
		return;
	}

	float otherRawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / otherLinearDepth) / SharedData::CameraData.z;
	Stereo::FinalizeStereoBlend(r, uv, rawDepth, otherRawDepth, eyeIndex, outFrameDim, kDepthSigma, kMaxBlend, kBackCheckThreshold);

	outAo[px] = lerp(srcAo[px], srcAo[r.otherPx], r.blendWeight);
#ifdef GI
	outIlY[px] = lerp(srcIlY[px], srcIlY[r.otherPx], r.blendWeight);
	outIlCoCg[px] = lerp(srcIlCoCg[px], srcIlCoCg[r.otherPx], r.blendWeight);
#endif
}
