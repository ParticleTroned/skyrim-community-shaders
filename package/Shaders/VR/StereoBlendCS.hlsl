// Stereo Bilateral Blend - post-composite stereo consistency pass for VR.
//
// The pass reprojects each pixel to the other eye, validates the match with
// depth and round-trip checks, and applies a low capped blend only where the
// two eyes visibly disagree. Edge checks from #1948 skip source and destination
// depth discontinuities to avoid halos at silhouettes and HMD-mask boundaries.

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

RWTexture2D<float4> OutputRW : register(u0);

cbuffer StereoBlendCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float DepthSigma;
	float MaxBlendFactor;
	float ColorDiffThreshold;
	float pad;
};

static const float kEdgeDepthThreshold = 0.05;
static const int kEdgeMargin = 2;

float4 SampleCrossDepths(int2 center, int offset, uint eyeIndex)
{
	return float4(
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(offset, 0), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(-offset, 0), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(0, offset), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(0, -offset), eyeIndex, FrameDim)]);
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID)
{
	if (any(dtid >= uint2(FrameDim)))
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float4 centerColor = ColorTexture[dtid];
	float centerDepth = DepthTexture[dtid];
	float4 blendedColor = centerColor;

	// depth == 0.0: VR HMD mask. depth == 1.0: sky/far plane.
	bool skipPixel = centerDepth < 1e-5 || centerDepth >= 1.0;
	if (!skipPixel) {
		float4 srcEdgeDepths = SampleCrossDepths(dtid, 1, eyeIndex);
		if (Stereo::MaxDepthDiff(centerDepth, srcEdgeDepths) <= kEdgeDepthThreshold) {
			Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
			if (r.valid) {
				float otherDepth = DepthTexture[r.otherPx];
				float4 dstEdgeDepths = SampleCrossDepths(r.otherPx, kEdgeMargin, 1 - eyeIndex);

				if (!any(dstEdgeDepths < 1e-5) && Stereo::MaxDepthDiff(otherDepth, dstEdgeDepths) <= kEdgeDepthThreshold) {
					float4 otherColor = ColorTexture[r.otherPx];
					Stereo::FinalizeStereoBlend(r, uv, centerDepth, otherDepth, eyeIndex, FrameDim, DepthSigma, MaxBlendFactor);

					float colorDiff = abs(dot(centerColor.rgb, float3(0.2126, 0.7152, 0.0722)) -
					                      dot(otherColor.rgb, float3(0.2126, 0.7152, 0.0722)));
					float colorThreshold = max(ColorDiffThreshold, 1e-5);
					float colorGate = smoothstep(colorThreshold * 0.5, colorThreshold * 2.0, colorDiff);
					r.blendWeight *= colorGate;

					blendedColor = lerp(centerColor, otherColor, r.blendWeight);
				}
			}
		}
	}

	OutputRW[dtid] = blendedColor;
}
