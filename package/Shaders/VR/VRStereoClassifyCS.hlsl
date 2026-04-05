#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"
#include "VR/StereoMode.hlsli"
#include "VR/StereoOptimizationCB.hlsli"

Texture2D<float> DepthTexture : register(t0);
RWTexture2D<uint> ModeTextureRW : register(u0);
static const float kSkyDepthEpsilon = 1e-5;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	const uint2 pixCoord = dispatchID.xy + uint2(VRStereoDispatchXOffsetPixels, 0u);
	if (any(pixCoord >= uint2(VRStereoRenderDim.xy)))
		return;

	const float2 uvDynamic = (float2(pixCoord) + 0.5) * VRStereoInvRenderDim;
	const float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uvDynamic);
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	// Keep left eye fully native.
	if (eyeIndex == 0u) {
		ModeTextureRW[pixCoord] = VR_STEREO_MODE_NATIVE;
		return;
	}

	const float depth = DepthTexture[pixCoord];
	const float2 monoUV = Stereo::ConvertFromStereoUV(uv, eyeIndex);
	const float3 reprojectedMono = Stereo::ConvertMonoUVToOtherEye(float3(monoUV, depth), eyeIndex);

	if (!VRStereoIsInsideFrame(reprojectedMono.xy)) {
		ModeTextureRW[pixCoord] = VR_STEREO_MODE_NATIVE;
		return;
	}

	const uint sourceEyeIndex = 1u - eyeIndex;
	float2 sourceStereoUV = Stereo::ConvertToStereoUV(reprojectedMono.xy, sourceEyeIndex);
	sourceStereoUV = VRStereoClampStereoUVToEye(sourceStereoUV, sourceEyeIndex);
	const float2 sourceStereoUVDynamic = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sourceStereoUV);
	const int2 sourceCoord = VRStereoUVToPixel(sourceStereoUVDynamic);
	const float sourceDepth = DepthTexture[sourceCoord];

	const float centerInfluence = VRStereoComputeCenterInfluence(monoUV, eyeIndex);
	const float adjustedDisocclusionThreshold = VRStereoDisocclusionThreshold * max(0.2, 1.0 - centerInfluence * VRStereoCenterProtection);
	const float maxRawDepth = max(max(depth, sourceDepth), EPSILON_DIVISION);
	const float rawRelDiff = abs(depth - sourceDepth) / maxRawDepth;
	bool disoccluded = rawRelDiff > adjustedDisocclusionThreshold;

	if (!disoccluded && eyeIndex == 1u && VRStereoForwardOcclusionScale > 0.0) {
		const bool centerIsSky = (depth < kSkyDepthEpsilon) || (depth >= 1.0);
		const bool sourceIsSky = (sourceDepth < kSkyDepthEpsilon) || (sourceDepth >= 1.0);
		if (!centerIsSky && !sourceIsSky) {
			const float linCenter = SharedData::GetScreenDepth(depth);
			const float linSource = SharedData::GetScreenDepth(sourceDepth);
			disoccluded = (linSource * VRStereoForwardOcclusionScale) < linCenter;
		}
	}

	const int edgeRadius = max(1, (int)round(VRStereoEdgeBandPixels));
	float maxDepthDelta = 0.0;
	[unroll]
	for (int i = 1; i <= 2; ++i) {
		if (i > edgeRadius)
			break;

		const int2 offsets[4] = {
			int2(i, 0),
			int2(-i, 0),
			int2(0, i),
			int2(0, -i)
		};
		[unroll]
		for (int j = 0; j < 4; ++j) {
			const int2 sampleCoord = clamp(int2(pixCoord) + offsets[j], int2(0, 0), int2(VRStereoRenderDim) - 1);
			const float sampleDepth = DepthTexture[sampleCoord];
			maxDepthDelta = max(maxDepthDelta, abs(depth - sampleDepth));
		}
	}

	const bool depthEdge = maxDepthDelta > VRStereoEdgeDepthThreshold;
	if (disoccluded || depthEdge) {
		ModeTextureRW[pixCoord] = VR_STEREO_MODE_NATIVE;
		return;
	}

	const float nearBlend = VRStereoComputeNearFieldBlendFactor(monoUV, depth, eyeIndex);
	const float centerBlend = saturate(centerInfluence * VRStereoCenterProtection);
	const float fullBlendWeight = max(centerBlend, nearBlend);

	ModeTextureRW[pixCoord] = (fullBlendWeight >= VRStereoCenterFullBlendThreshold) ?
		VR_STEREO_MODE_FULL_BLEND :
		VR_STEREO_MODE_REPROJECT;
}
