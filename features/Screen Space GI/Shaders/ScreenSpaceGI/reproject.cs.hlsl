// Stereo reproject for view-independent SSGI outputs.

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/StereoReproject.hlsli"
#include "ScreenSpaceGI/common.hlsli"

#ifdef VR

Texture2D<float> srcDepth : register(t0);
Texture2D<float> srcAo : register(t1);
#	ifdef GI
Texture2D<float4> srcIlY : register(t2);
Texture2D<float2> srcIlCoCg : register(t3);
#	endif

RWTexture2D<float> outAo : register(u0);
#	ifdef GI
RWTexture2D<float4> outIlY : register(u1);
RWTexture2D<float2> outIlCoCg : register(u2);
#	endif

void Passthrough(uint2 dtid)
{
	outAo[dtid] = srcAo[dtid];
#	ifdef GI
	outIlY[dtid] = srcIlY[dtid];
	outIlCoCg[dtid] = srcIlCoCg[dtid];
#	endif
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	const float2 outFrameDim = OUT_FRAME_DIM;
	if (any(dtid >= uint2(outFrameDim)))
		return;

	if (StereoEnabled == 0) {
		Passthrough(dtid);
		return;
	}

	const float2 frameScale = FrameDim * RcpTexDim;
	float2 uv = (dtid + 0.5) / outFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	if (eyeIndex == 0) {
		Passthrough(dtid);
		return;
	}

	float depth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, RES_MIP);
	int2 otherPx;
	if (GIReprojectsCleanly(uv, depth, eyeIndex, srcDepth, frameScale, otherPx)) {
		outAo[dtid] = srcAo[otherPx];
#	ifdef GI
		outIlY[dtid] = srcIlY[otherPx];
		outIlCoCg[dtid] = srcIlCoCg[otherPx];
#	endif
	} else {
		Passthrough(dtid);
	}
}

#endif
