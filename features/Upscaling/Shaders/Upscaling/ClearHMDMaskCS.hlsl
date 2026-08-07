// Zeros color in the HMD hidden area for a single eye region.
// Prevents DLSS/FSR from temporally accumulating the engine's sky/ambient clear color
// into visible pixels during head movement ("light blue border" ghosting).
// depth ~= 0.0 is the unrendered/hidden area value (Skyrim reversed-Z: far plane = 0).
// Expands the mask in depth space so bilinear/current-neighborhood and upscaler
// reconstruction taps cannot pull hidden-area clear color back across the edge.
// DepthIn can be a packed stereo depth buffer or another depth source. The shader
// supports direct eye-local addressing or scaled color->depth coordinate mapping.

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

bool IsHiddenDepth(float depth)
{
	return depth <= kHiddenDepthThreshold;
}

static const uint kProbeGridSize = 9;

uint ResolveProbeCoordinate(uint index, uint extent)
{
	// Exact integer form of floor(((index + 0.5) / kProbeGridSize) * extent).
	return min(((2 * index + 1) * extent) / (2 * kProbeGridSize), extent - 1);
}

[numthreads(9, 9, 1)] void DevBenchHAMProbeMain(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= kProbeGridSize || dispatchID.y >= kProbeGridSize ||
		ColorWidth == 0 || ColorHeight == 0 || DepthWidth == 0 || DepthHeight == 0) {
		return;
	}

	uint depthTextureWidth, depthTextureHeight;
	DepthIn.GetDimensions(depthTextureWidth, depthTextureHeight);

	uint2 localColorPos = uint2(
		ResolveProbeCoordinate(dispatchID.x, ColorWidth),
		ResolveProbeCoordinate(dispatchID.y, ColorHeight));
	uint2 depthPos = uint2(
		(localColorPos.x * DepthWidth) / ColorWidth,
		(localColorPos.y * DepthHeight) / ColorHeight) +
		uint2(DepthOffsetX, DepthOffsetY);

	if (depthPos.x >= depthTextureWidth || depthPos.y >= depthTextureHeight) {
		ColorInOut[dispatchID.xy] = float4(-1.0, -1.0, 0.0, 0.0);
		return;
	}

	const float centerDepth = DepthIn[depthPos];
	float minimumDepth = centerDepth;
	uint hiddenSampleCount = 0;
	[unroll]
	for (int y = -kHiddenDepthDilationRadius; y <= kHiddenDepthDilationRadius; ++y) {
		[unroll]
		for (int x = -kHiddenDepthDilationRadius; x <= kHiddenDepthDilationRadius; ++x) {
			int2 samplePos = int2(depthPos) + int2(x, y);
			if (any(samplePos < int2(0, 0)) ||
				samplePos.x >= int(depthTextureWidth) ||
				samplePos.y >= int(depthTextureHeight)) {
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

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= ColorWidth || dispatchID.y >= ColorHeight)
		return;

	uint colorTexWidth, colorTexHeight;
	ColorInOut.GetDimensions(colorTexWidth, colorTexHeight);

	uint2 colorPos = dispatchID.xy + uint2(ColorOffsetX, ColorOffsetY);
	if (colorPos.x >= colorTexWidth || colorPos.y >= colorTexHeight)
		return;

	uint depthTexWidth, depthTexHeight;
	DepthIn.GetDimensions(depthTexWidth, depthTexHeight);

	uint2 depthPos;
	if (DepthWidth > 0 && DepthHeight > 0 && ColorWidth > 0 && ColorHeight > 0) {
		depthPos = uint2(
			(dispatchID.x * DepthWidth) / ColorWidth,
			(dispatchID.y * DepthHeight) / ColorHeight) +
			uint2(DepthOffsetX, DepthOffsetY);
	} else {
		depthPos = dispatchID.xy + uint2(DepthOffsetX, DepthOffsetY);
	}

	if (depthPos.x >= depthTexWidth || depthPos.y >= depthTexHeight)
		return;

	bool clearPixel = false;
	[unroll]
	for (int y = -kHiddenDepthDilationRadius; y <= kHiddenDepthDilationRadius; ++y) {
		[unroll]
		for (int x = -kHiddenDepthDilationRadius; x <= kHiddenDepthDilationRadius; ++x) {
			if (clearPixel)
				continue;

			int2 samplePos = int2(depthPos) + int2(x, y);
			if (any(samplePos < int2(0, 0)) || samplePos.x >= int(depthTexWidth) || samplePos.y >= int(depthTexHeight))
				continue;

			if (IsHiddenDepth(DepthIn[uint2(samplePos)])) {
				clearPixel = true;
				break;
			}
		}
	}

	if (clearPixel)
		ColorInOut[colorPos] = float4(0.0, 0.0, 0.0, 0.0);
}
