#include "Common/FoveatedMask.hlsli"

cbuffer AAVRSVisualizationCB : register(b0)
{
	float4 RenderInfo;     // xy=render dim, zw=1/render dim
	float4 DisplayInfo;    // xy=display dim, z=eye count, w=coarseOutsideMask
	float4 MaskInfo;       // x=center area, y=protected outer area, z=center horizontal scale
	float4 CenterOffsets;  // xy=left eye, zw=right eye
	float4 CoarseColor;
	float4 CenterColor;
	float4 Pad;            // xy=VRS tile size, z=performance mode, w=performance anisotropy
};

RWTexture2D<float4> OutColor : register(u0);

static const uint kRateIndex1x1 = 0;
static const uint kRateIndex2x1 = 1;
static const uint kRateIndex1x2 = 2;
static const uint kRateIndex2x2 = 3;
static const uint kRateIndex4x4 = 4;
static const float kPerformanceModeFullRateScale = 0.25;
static const float kPerformanceModeAnisotropicScale = 0.40;
static const float kPerformanceModeTwoByTwoScale = 0.70;
static const uint kPerformanceAnisotropy2x1 = 1;
static const uint kPerformanceAnisotropy1x2 = 2;

float AAVRSTileMinMaskDistance(
	float2 tileMin,
	float2 tileMax,
	float renderScaleX,
	float renderScaleY,
	float eyeDisplayWidth,
	float displayHeight,
	float maskArea,
	float centerHorizontalScale,
	float2 centerOffset)
{
	const float minDisplayX = clamp(tileMin.x / renderScaleX, 0.0, eyeDisplayWidth);
	const float maxDisplayX = clamp(tileMax.x / renderScaleX, 0.0, eyeDisplayWidth);
	const float minDisplayY = clamp(tileMin.y / renderScaleY, 0.0, displayHeight);
	const float maxDisplayY = clamp(tileMax.y / renderScaleY, 0.0, displayHeight);
	const float invEyeDisplayWidth = 1.0 / max(eyeDisplayWidth, 1.0);
	const float invDisplayHeight = 1.0 / max(displayHeight, 1.0);
	const float2 minUV = float2(minDisplayX * invEyeDisplayWidth, minDisplayY * invDisplayHeight);
	const float2 maxUV = float2(maxDisplayX * invEyeDisplayWidth, maxDisplayY * invDisplayHeight);
	const float2 centerUV = FoveatedComputeCenterUV(centerOffset);
	return FoveatedComputeMaskDistance(clamp(centerUV, minUV, maxUV), maskArea, centerHorizontalScale, centerOffset);
}

uint AAVRSChooseConservativeStereoRate(uint leftRate, uint rightRate)
{
	if (leftRate == rightRate)
		return leftRate;
	if (leftRate == kRateIndex1x1 || rightRate == kRateIndex1x1)
		return kRateIndex1x1;
	if (leftRate == kRateIndex4x4)
		return rightRate;
	if (rightRate == kRateIndex4x4)
		return leftRate;
	if ((leftRate == kRateIndex2x1 && rightRate == kRateIndex1x2) ||
		(leftRate == kRateIndex1x2 && rightRate == kRateIndex2x1))
		return kRateIndex1x1;
	return kRateIndex2x2;
}

uint AAVRSResolvePerformanceAnisotropicRate(uint performanceAnisotropy, float2 tileCenter, float2 center)
{
	if (performanceAnisotropy == kPerformanceAnisotropy2x1)
		return kRateIndex2x1;
	if (performanceAnisotropy == kPerformanceAnisotropy1x2)
		return kRateIndex1x2;

	return abs(tileCenter.x - center.x) >= abs(tileCenter.y - center.y) ? kRateIndex2x1 : kRateIndex1x2;
}

uint AAVRSPerformanceRateIndex(
	float2 tileMin,
	float2 tileMax,
	float renderScaleX,
	float renderScaleY,
	float eyeDisplayWidth,
	float displayHeight,
	float centerHorizontalScale,
	float2 centerOffset,
	uint performanceAnisotropy)
{
	if (AAVRSTileMinMaskDistance(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			kPerformanceModeFullRateScale,
			centerHorizontalScale,
			centerOffset) <= 1.0)
		return kRateIndex1x1;

	if (AAVRSTileMinMaskDistance(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			kPerformanceModeAnisotropicScale,
			centerHorizontalScale,
			centerOffset) <= 1.0) {
		const float2 tileCenter = ((tileMin + tileMax) * 0.5) / float2(renderScaleX, renderScaleY);
		const float2 center = FoveatedComputeCenterUV(centerOffset) * float2(eyeDisplayWidth, displayHeight);
		return AAVRSResolvePerformanceAnisotropicRate(performanceAnisotropy, tileCenter, center);
	}

	if (AAVRSTileMinMaskDistance(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			kPerformanceModeTwoByTwoScale,
			centerHorizontalScale,
			centerOffset) <= 1.0)
		return kRateIndex2x2;

	return kRateIndex4x4;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	const uint2 renderDim = (uint2)(RenderInfo.xy + 0.5);
	if (any(dispatchID.xy >= renderDim))
		return;

	const uint eyeCount = max((uint)(DisplayInfo.z + 0.5), 1u);
	const float eyeRenderWidth = max(RenderInfo.x / (float)eyeCount, 1.0);
	const float eyeDisplayWidth = max(DisplayInfo.x / (float)eyeCount, 1.0);
	const float displayHeight = max(DisplayInfo.y, 1.0);
	const float renderScaleX = max(RenderInfo.x / max(DisplayInfo.x, 1.0), 1e-4);
	const float renderScaleY = max(RenderInfo.y / max(DisplayInfo.y, 1.0), 1e-4);
	const float centerArea = MaskInfo.x;
	const float outerArea = max(MaskInfo.y, centerArea);
	const bool performanceMode = Pad.z > 0.5;
	const uint performanceAnisotropy = min((uint)(Pad.w + 0.5), kPerformanceAnisotropy1x2);
	const float protectedArea = performanceMode ? kPerformanceModeFullRateScale : (DisplayInfo.w > 0.5 ? outerArea : centerArea);
	const float centerHorizontalScale = MaskInfo.z;

	const float2 tileSize = max(Pad.xy, float2(1.0, 1.0));
	const uint eye = min((uint)((float)dispatchID.x / eyeRenderWidth), eyeCount - 1u);
	const float eyeOffsetX = eyeRenderWidth * (float)eye;
	const float2 localPixel = float2((float)dispatchID.x - eyeOffsetX, (float)dispatchID.y);
	const float2 tileMin = floor(localPixel / tileSize) * tileSize;
	const float2 tileMax = min(tileMin + tileSize, float2(eyeRenderWidth, RenderInfo.y));
	const float2 eyeCenterOffset = eye == 0u ? CenterOffsets.xy : CenterOffsets.zw;

	if (performanceMode) {
		uint rateIndex = AAVRSPerformanceRateIndex(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			centerHorizontalScale,
			eyeCenterOffset,
			performanceAnisotropy);
		if (eyeCount > 1u) {
			const uint leftRate = AAVRSPerformanceRateIndex(
				tileMin,
				tileMax,
				renderScaleX,
				renderScaleY,
				eyeDisplayWidth,
				displayHeight,
				centerHorizontalScale,
				CenterOffsets.xy,
				performanceAnisotropy);
			const uint rightRate = AAVRSPerformanceRateIndex(
				tileMin,
				tileMax,
				renderScaleX,
				renderScaleY,
				eyeDisplayWidth,
				displayHeight,
				centerHorizontalScale,
				CenterOffsets.zw,
				performanceAnisotropy);
			rateIndex = AAVRSChooseConservativeStereoRate(leftRate, rightRate);
		}
		OutColor[dispatchID.xy] = rateIndex == kRateIndex1x1 ? CenterColor : CoarseColor;
		return;
	}

	if (protectedArea >= 0.999) {
		OutColor[dispatchID.xy] = CenterColor;
		return;
	}

	float protectedDistance = AAVRSTileMinMaskDistance(
		tileMin,
		tileMax,
		renderScaleX,
		renderScaleY,
		eyeDisplayWidth,
		displayHeight,
		protectedArea,
		centerHorizontalScale,
		eyeCenterOffset);
	if (eyeCount > 1u) {
		const float leftDistance = AAVRSTileMinMaskDistance(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			protectedArea,
			centerHorizontalScale,
			CenterOffsets.xy);
		const float rightDistance = AAVRSTileMinMaskDistance(
			tileMin,
			tileMax,
			renderScaleX,
			renderScaleY,
			eyeDisplayWidth,
			displayHeight,
			protectedArea,
			centerHorizontalScale,
			CenterOffsets.zw);
		protectedDistance = min(leftDistance, rightDistance);
	}

	if (protectedDistance <= 1.0) {
		OutColor[dispatchID.xy] = CenterColor;
		return;
	}

	OutColor[dispatchID.xy] = CoarseColor;
}
