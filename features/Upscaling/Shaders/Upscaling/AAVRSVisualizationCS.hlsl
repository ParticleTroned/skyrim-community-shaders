#include "Common/FoveatedMask.hlsli"

cbuffer AAVRSVisualizationCB : register(b0)
{
	float4 RenderInfo;     // xy=render dim, zw=1/render dim
	float4 DisplayInfo;    // xy=display dim, z=eye count, w=coarseOutsideMask
	float4 MaskInfo;       // x=center area, y=outer area, z=center horizontal scale
	float4 CenterOffsets;  // xy=left eye, zw=right eye
	float4 CoarseColor;
	float4 CenterColor;
	float4 Pad;
};

RWTexture2D<float4> OutColor : register(u0);

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
	const float centerHorizontalScale = MaskInfo.z;

	if (centerArea >= 0.999) {
		OutColor[dispatchID.xy] = CenterColor;
		return;
	}

	const float2 tileSize = max(Pad.xy, float2(1.0, 1.0));
	const uint eye = min((uint)((float)dispatchID.x / eyeRenderWidth), eyeCount - 1u);
	const float eyeOffsetX = eyeRenderWidth * (float)eye;
	const float2 localPixel = float2((float)dispatchID.x - eyeOffsetX, (float)dispatchID.y);
	const float2 tileMin = floor(localPixel / tileSize) * tileSize;
	const float2 tileMax = min(tileMin + tileSize, float2(eyeRenderWidth, RenderInfo.y));
	const float leftCenterDistance = AAVRSTileMinMaskDistance(
		tileMin,
		tileMax,
		renderScaleX,
		renderScaleY,
		eyeDisplayWidth,
		displayHeight,
		centerArea,
		centerHorizontalScale,
		CenterOffsets.xy);
	const float rightCenterDistance = AAVRSTileMinMaskDistance(
		tileMin,
		tileMax,
		renderScaleX,
		renderScaleY,
		eyeDisplayWidth,
		displayHeight,
		centerArea,
		centerHorizontalScale,
		CenterOffsets.zw);

	if (leftCenterDistance <= 1.0 || (eyeCount > 1u && rightCenterDistance <= 1.0)) {
		OutColor[dispatchID.xy] = CenterColor;
		return;
	}

	OutColor[dispatchID.xy] = CoarseColor;
}
