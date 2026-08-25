// Zeros color in the HMD hidden area for a single eye region.
// Prevents temporal upscalers from accumulating hidden-area clear color into
// visible pixels during head movement. Skyrim uses reversed Z, so depth near
// zero identifies the unrendered region.
//
// The host accepts only equal-size or upscaling color mappings. An 8x8 color
// group therefore spans at most eight depth texels, and a 12x12 shared tile
// covers the exact radius-two halo.

cbuffer ClearHMDMaskCB : register(b0)
{
	uint DepthOffsetX;
	uint ColorOffsetX;
	uint DepthOffsetY;
	uint ColorOffsetY;
	uint DepthWidth;
	uint DepthHeight;
	uint ColorWidth;
	uint ColorHeight;
};

Texture2D<float> DepthIn : register(t0);
RWTexture2D<float4> ColorInOut : register(u0);

static const float kHiddenDepthThreshold = 1e-6;
static const int kHiddenDepthDilationRadius = 2;
static const uint kProbeGridSize = 9;

bool IsHiddenDepth(float depth)
{
	return depth <= kHiddenDepthThreshold;
}

bool IsDepthSampleInRegion(
	int2 samplePos,
	uint2 depthDimensions,
	uint2 depthOffset,
	uint2 depthExtent)
{
	if (any(samplePos < int2(0, 0)))
		return false;

	const uint2 unsignedSamplePos = uint2(samplePos);
	if (any(unsignedSamplePos >= depthDimensions) ||
		any(unsignedSamplePos < depthOffset)) {
		return false;
	}

	return all(unsignedSamplePos - depthOffset < depthExtent);
}

uint ResolveProbeCoordinate(uint index, uint extent)
{
	// Exact integer form of floor(((index + 0.5) / kProbeGridSize) * extent).
	return min(((2 * index + 1) * extent) / (2 * kProbeGridSize), extent - 1);
}

[numthreads(9, 9, 1)] void DevBenchHAMProbeMain(uint3 dispatchID : SV_DispatchThreadID) {
	if (dispatchID.x >= kProbeGridSize || dispatchID.y >= kProbeGridSize ||
		ColorWidth == 0 || ColorHeight == 0 ||
		DepthWidth == 0 || DepthHeight == 0) {
		return;
	}

	uint depthTextureWidth, depthTextureHeight;
	DepthIn.GetDimensions(depthTextureWidth, depthTextureHeight);
	const uint2 depthDimensions = uint2(depthTextureWidth, depthTextureHeight);
	const uint2 depthOffset = uint2(DepthOffsetX, DepthOffsetY);
	const uint2 depthExtent = uint2(DepthWidth, DepthHeight);
	const uint2 localColorPos = uint2(
		ResolveProbeCoordinate(dispatchID.x, ColorWidth),
		ResolveProbeCoordinate(dispatchID.y, ColorHeight));
	const uint2 depthPos = uint2(
							   (localColorPos.x * DepthWidth) / ColorWidth,
							   (localColorPos.y * DepthHeight) / ColorHeight) +
	                       depthOffset;

	if (!IsDepthSampleInRegion(
			int2(depthPos), depthDimensions, depthOffset, depthExtent)) {
		ColorInOut[dispatchID.xy] = float4(-1.0, -1.0, 0.0, 0.0);
		return;
	}

	const float centerDepth = DepthIn[depthPos];
	float minimumDepth = centerDepth;
	uint hiddenSampleCount = 0;
	[unroll] for (int y = -kHiddenDepthDilationRadius;
		y <= kHiddenDepthDilationRadius;
		++y)
	{
		[unroll] for (int x = -kHiddenDepthDilationRadius;
			x <= kHiddenDepthDilationRadius;
			++x)
		{
			const int2 samplePos = int2(depthPos) + int2(x, y);
			if (!IsDepthSampleInRegion(
					samplePos, depthDimensions, depthOffset, depthExtent)) {
				continue;
			}
			const float depth = DepthIn[uint2(samplePos)];
			minimumDepth = min(minimumDepth, depth);
			if (IsHiddenDepth(depth))
				++hiddenSampleCount;
		}
	}

	ColorInOut[dispatchID.xy] = float4(
		centerDepth,
		minimumDepth,
		float(hiddenSampleCount),
		hiddenSampleCount != 0 ? 1.0 : 0.0);
}

groupshared uint HiddenDepthTile[12 * 12];

[numthreads(8, 8, 1)] void main(
	uint3 groupID : SV_GroupID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint3 dispatchID : SV_DispatchThreadID) {
	if (DepthWidth == 0 || DepthHeight == 0 ||
		ColorWidth == 0 || ColorHeight == 0) {
		return;
	}

	uint depthTextureWidth, depthTextureHeight;
	DepthIn.GetDimensions(depthTextureWidth, depthTextureHeight);
	const uint2 depthDimensions = uint2(depthTextureWidth, depthTextureHeight);
	const uint2 depthOffset = uint2(DepthOffsetX, DepthOffsetY);
	const uint2 depthExtent = uint2(DepthWidth, DepthHeight);

	const uint2 groupColorMin = groupID.xy * 8u;
	const uint2 groupColorMax = min(
		groupColorMin + 7u,
		uint2(ColorWidth - 1u, ColorHeight - 1u));
	const uint2 groupDepthMin = uint2(
									(groupColorMin.x * DepthWidth) / ColorWidth,
									(groupColorMin.y * DepthHeight) / ColorHeight) +
	                            depthOffset;
	const uint2 groupDepthMax = uint2(
									(groupColorMax.x * DepthWidth) / ColorWidth,
									(groupColorMax.y * DepthHeight) / ColorHeight) +
	                            depthOffset;
	const uint2 tileExtent = groupDepthMax - groupDepthMin + 5u;
	const int2 tileOrigin =
		int2(groupDepthMin) - kHiddenDepthDilationRadius;
	const uint linearThread =
		groupThreadID.y * 8u + groupThreadID.x;
	const uint tileElementCount = tileExtent.x * tileExtent.y;

	for (uint index = linearThread;
		index < tileElementCount;
		index += 64u) {
		const uint2 tilePos =
			uint2(index % tileExtent.x, index / tileExtent.x);
		const int2 samplePos = tileOrigin + int2(tilePos);
		bool hidden = false;
		if (IsDepthSampleInRegion(
				samplePos, depthDimensions, depthOffset, depthExtent)) {
			hidden = IsHiddenDepth(DepthIn[uint2(samplePos)]);
		}
		HiddenDepthTile[index] = hidden ? 1u : 0u;
	}
	GroupMemoryBarrierWithGroupSync();

	if (dispatchID.x >= ColorWidth || dispatchID.y >= ColorHeight)
		return;

	const uint2 depthPos = uint2(
							   (dispatchID.x * DepthWidth) / ColorWidth,
							   (dispatchID.y * DepthHeight) / ColorHeight) +
	                       depthOffset;
	if (!IsDepthSampleInRegion(
			int2(depthPos), depthDimensions, depthOffset, depthExtent)) {
		return;
	}

	const uint2 tileCenter = uint2(int2(depthPos) - tileOrigin);
	bool clearPixel = false;
	[unroll] for (int y = -kHiddenDepthDilationRadius;
		y <= kHiddenDepthDilationRadius;
		++y)
	{
		[unroll] for (int x = -kHiddenDepthDilationRadius;
			x <= kHiddenDepthDilationRadius;
			++x)
		{
			const uint2 tilePos =
				uint2(int2(tileCenter) + int2(x, y));
			clearPixel = clearPixel ||
			             HiddenDepthTile[tilePos.y * tileExtent.x + tilePos.x] != 0u;
		}
	}

	uint colorTextureWidth, colorTextureHeight;
	ColorInOut.GetDimensions(colorTextureWidth, colorTextureHeight);
	const uint2 colorPos =
		dispatchID.xy + uint2(ColorOffsetX, ColorOffsetY);
	if (clearPixel &&
		colorPos.x < colorTextureWidth &&
		colorPos.y < colorTextureHeight) {
		ColorInOut[colorPos] = float4(0.0, 0.0, 0.0, 0.0);
	}
}
