#include "Common/FoveatedMask.hlsli"
#include "Upscaling/FoveatedMaskVisualization.hlsli"

cbuffer FoveatedPeripheryCB : register(b0)
{
	float2 OutputDim;
	float2 InvOutputDim;
	float2 InvSourceDim;
	float2 SourceScale;
	float2 SourceOffset;
	float2 DispatchDim;
	float2 OutputOffset;
	float2 Jitter;
	float4 CenterAndMask;  // xy=centerOffset, z=visualizeMask, w=showThreeZoneMask
	float4 Tuning0;        // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=taaOuterScale
	float4 Preview;        // xy=other eye offset, z=eye index, w=full-image coverage
	row_major float4x4 PreviewClipToOtherEye;
	float4 PreviewArea;  // x=percentage of full eye area saved, y=validated stereo projection
};

Texture2D<float4> InputColor : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutColor : register(u0);

float2 ClampToSourceRegion(float2 uv, float2 regionMin, float2 regionMax)
{
	return clamp(uv, regionMin, regionMax);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint2 localPos = dispatchID.xy;
	if (any(localPos >= uint2(DispatchDim)))
		return;

	uint2 outputPos = localPos + uint2(OutputOffset + 0.5);
	if (any(outputPos >= uint2(OutputDim)))
		return;

	float2 uv = (float2(outputPos) + 0.5) * InvOutputDim;

	const float centerScale = Tuning0.x;
	const float centerFeather = Tuning0.y;
	const float centerHorizontalScale = Tuning0.z;
	const float2 centerOffset = CenterAndMask.xy;
	const float visualizeMask = CenterAndMask.z;
	const float showThreeZoneMask = CenterAndMask.w;
	const float taaNormalizedFeather = FoveatedComputeNormalizedFeather(centerScale, centerFeather, centerHorizontalScale);
	const float minOuterScale = centerScale * (1.0 + taaNormalizedFeather);
	const float taaOuterScale = min(max(Tuning0.w, minOuterScale), 1.0);

	float2 sourceRegionMin = SourceOffset;
	float2 sourceRegionMax = SourceOffset + SourceScale;
	float2 halfTexel = InvSourceDim * 0.5;
	sourceRegionMin = min(sourceRegionMin + halfTexel, sourceRegionMax);
	sourceRegionMax = max(sourceRegionMax - halfTexel, sourceRegionMin);

	float2 sourceUV = (uv * SourceScale + SourceOffset) - (Jitter * InvSourceDim);
	sourceUV = ClampToSourceRegion(sourceUV, sourceRegionMin, sourceRegionMax);

	float4 color = InputColor.SampleLevel(LinearSampler, sourceUV, 0.0);
	if (visualizeMask > 0.5) {
		const bool fullCoverage = Preview.w > 0.5;
		const bool inCenter = fullCoverage || FoveatedComputeMaskDistance(uv, centerScale, centerHorizontalScale, centerOffset) <= 1.0 + taaNormalizedFeather;
		const bool inTaa = showThreeZoneMask > 0.5 && FoveatedComputeMaskDistance(uv, taaOuterScale, centerHorizontalScale, centerOffset) <= 1.0;

		const float4 otherClip = mul(PreviewClipToOtherEye, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0));
		const float2 otherUV = otherClip.xy / max(otherClip.w, 1e-6) * float2(0.5, -0.5) + 0.5;
		const bool projectionKnown = PreviewArea.y > 0.5 && all(isfinite(otherClip)) && any(abs(otherClip) > 1e-6);
		const bool otherVisible = projectionKnown && otherClip.w > 1e-6 && all(otherUV >= 0.0) && all(otherUV <= 1.0);
		const bool inOtherCenter = otherVisible && (fullCoverage || FoveatedComputeMaskDistance(otherUV, centerScale, centerHorizontalScale, Preview.xy) <= 1.0 + taaNormalizedFeather);
		const bool inOtherTaa = otherVisible && showThreeZoneMask > 0.5 && FoveatedComputeMaskDistance(otherUV, taaOuterScale, centerHorizontalScale, Preview.xy) <= 1.0;
		// Static hatching retains world context while making either eye's missing coverage unambiguous.
		const bool uncovered = !projectionKnown || (!inCenter && !inTaa) || (otherVisible && !inOtherCenter && !inOtherTaa);
		// Magenta/green separate along blue as well as red-green; overlap is neutral and brighter.
		static const float3 kLeftColor = float3(1.0, 0.1, 1.0);
		static const float3 kRightColor = float3(0.1, 1.0, 0.1);
		static const float3 kOverlapColor = float3(1.0, 1.0, 1.0);
		static const float3 kTaaColor = float3(0.04, 0.04, 0.04);
		static const float kTintAlpha = 0.18;
		if (uncovered) {
			const float stripeWidth = max(6.0, min(OutputDim.x, OutputDim.y) / 100.0);
			const bool stripe = fmod(floor((outputPos.x + outputPos.y) / stripeWidth), 2.0) < 1.0;
			const float3 hatch = stripe ? float3(0.65, 0.5, 0.05) : float3(0.08, 0.06, 0.02);
			color.rgb = lerp(color.rgb, hatch, 0.8);
		} else {
			const float3 tint = inCenter ? (inOtherCenter ? kOverlapColor : (Preview.z < 0.5 ? kLeftColor : kRightColor)) : kTaaColor;
			color.rgb = lerp(color.rgb, tint, kTintAlpha);
		}
		if (projectionKnown && !fullCoverage && (inCenter || inTaa)) {
			const float supportScale = max(centerScale + 2.0 * max(centerFeather, 1e-4), showThreeZoneMask > 0.5 ? taaOuterScale : 0.0);
			const float distance = FoveatedComputeMaskDistance(uv, centerScale, centerHorizontalScale, centerOffset) * centerScale / supportScale;
			const float edgePixels = (1.0 - distance) * supportScale * 0.5 * min(OutputDim.y, OutputDim.x * centerHorizontalScale);
			const float lineWidth = max(2.0, min(OutputDim.x, OutputDim.y) / 700.0);
			if (edgePixels <= lineWidth * 2.0)
				color.rgb = edgePixels <= lineWidth ? (Preview.z < 0.5 ? kLeftColor : kRightColor) : float3(0.015, 0.015, 0.015);
		}
		if (!uncovered)
			color.rgb = FoveatedPreviewAreaLabel(color.rgb, float2(outputPos) + 0.5, OutputDim,
				fullCoverage ? float2(0.5, 0.5) : FoveatedComputeCenterUV(centerOffset), PreviewArea.x);
	}
	OutColor[outputPos] = color;
}
