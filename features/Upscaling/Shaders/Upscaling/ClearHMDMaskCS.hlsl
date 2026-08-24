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
	uint AuditMaskWidth;
	uint AuditMaskHeight;
	uint AuditCandidateMode;
	uint AuditPadding;
};

Texture2D<float> DepthIn : register(t0);
Texture2D<uint> ReducedHMDMaskIn : register(t1);
RWTexture2D<float4> ColorInOut : register(u0);
RWTexture2D<uint> ReducedHMDMaskOut : register(u1);
RWByteAddressBuffer HMDMaskAuditCounters : register(u2);

static const float kHiddenDepthThreshold = 1e-6;
static const int kHiddenDepthDilationRadius = 2;
static const uint kAuditCandidateRobust = 0;
static const uint kAuditCandidateLegacySingleSample = 1;
static const uint kAuditCandidateReducedMask = 2;
static const uint kAuditSampleStride = 2;

bool IsHiddenDepth(float depth)
{
	return depth <= kHiddenDepthThreshold;
}

bool IsDilatedHiddenDepth(uint2 depthPos, uint2 depthDimensions)
{
	bool hidden = false;
	[unroll]
	for (int y = -kHiddenDepthDilationRadius; y <= kHiddenDepthDilationRadius; ++y) {
		[unroll]
		for (int x = -kHiddenDepthDilationRadius; x <= kHiddenDepthDilationRadius; ++x) {
			if (hidden)
				continue;

			int2 samplePos = int2(depthPos) + int2(x, y);
			if (any(samplePos < int2(0, 0)) ||
				samplePos.x >= int(depthDimensions.x) ||
				samplePos.y >= int(depthDimensions.y)) {
				continue;
			}

			if (IsHiddenDepth(DepthIn[uint2(samplePos)])) {
				hidden = true;
				break;
			}
		}
	}

	return hidden;
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

#if defined(HMD_MASK_LEGACY_SINGLE_SAMPLE)
	const bool clearPixel = DepthIn[depthPos] == 0.0;
#else
	const bool clearPixel = IsDilatedHiddenDepth(depthPos, uint2(depthTexWidth, depthTexHeight));
#endif

	if (clearPixel)
		ColorInOut[colorPos] = float4(0.0, 0.0, 0.0, 0.0);
}

// Builds the same conservative mask as the robust final scrub while color is
// still at vendor-input resolution. The resulting mask is reused after temporal
// reconstruction, and this pass also prevents hidden-area color from entering
// vendor history in the first place.
[numthreads(8, 8, 1)] void BuildReducedHMDMaskAndClearInputMain(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= ColorWidth || dispatchID.y >= ColorHeight)
		return;

	uint colorTexWidth, colorTexHeight;
	ColorInOut.GetDimensions(colorTexWidth, colorTexHeight);
	uint maskTexWidth, maskTexHeight;
	ReducedHMDMaskOut.GetDimensions(maskTexWidth, maskTexHeight);

	const uint2 localPos = dispatchID.xy;
	const uint2 colorPos = localPos + uint2(ColorOffsetX, ColorOffsetY);
	if (colorPos.x >= colorTexWidth || colorPos.y >= colorTexHeight ||
		localPos.x >= maskTexWidth || localPos.y >= maskTexHeight) {
		return;
	}

	uint depthTexWidth, depthTexHeight;
	DepthIn.GetDimensions(depthTexWidth, depthTexHeight);
	uint2 depthPos;
	if (DepthWidth > 0 && DepthHeight > 0 && ColorWidth > 0 && ColorHeight > 0) {
		depthPos = uint2(
			(localPos.x * DepthWidth) / ColorWidth,
			(localPos.y * DepthHeight) / ColorHeight) +
			uint2(DepthOffsetX, DepthOffsetY);
	} else {
		depthPos = localPos + uint2(DepthOffsetX, DepthOffsetY);
	}

	if (depthPos.x >= depthTexWidth || depthPos.y >= depthTexHeight) {
		ReducedHMDMaskOut[localPos] = 0;
		return;
	}

	const bool clearPixel = IsDilatedHiddenDepth(depthPos, uint2(depthTexWidth, depthTexHeight));
	ReducedHMDMaskOut[localPos] = clearPixel ? 1u : 0u;
	if (clearPixel)
		ColorInOut[colorPos] = float4(0.0, 0.0, 0.0, 0.0);
}

// The display-resolution safety scrub maps each output pixel back to the
// conservative reduced-resolution mask and performs exactly one mask load.
[numthreads(8, 8, 1)] void ClearHMDMaskFromReducedMain(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= ColorWidth || dispatchID.y >= ColorHeight ||
		DepthWidth == 0 || DepthHeight == 0) {
		return;
	}

	uint colorTexWidth, colorTexHeight;
	ColorInOut.GetDimensions(colorTexWidth, colorTexHeight);
	const uint2 colorPos = dispatchID.xy + uint2(ColorOffsetX, ColorOffsetY);
	if (colorPos.x >= colorTexWidth || colorPos.y >= colorTexHeight)
		return;

	uint maskTexWidth, maskTexHeight;
	ReducedHMDMaskIn.GetDimensions(maskTexWidth, maskTexHeight);
	const uint2 maskPos = uint2(
		(dispatchID.x * DepthWidth) / ColorWidth,
		(dispatchID.y * DepthHeight) / ColorHeight);
	if (maskPos.x >= maskTexWidth || maskPos.y >= maskTexHeight)
		return;

	if (ReducedHMDMaskIn[maskPos] != 0u)
		ColorInOut[colorPos] = float4(0.0, 0.0, 0.0, 0.0);
}

// DevBench-only decision audit. It never writes presentation color. A stable
// 2x2 stratified sample keeps captures representative and bounded while still
// evaluating the display-domain mapping used by the production scrub.
// Counter layout (uint32): evaluated, robust-clear, candidate-clear,
// false-negative, false-positive, mismatch, invalid-mask-lookup, dispatches.
[numthreads(8, 8, 1)] void DevBenchHMDMaskQualityMain(uint3 dispatchID : SV_DispatchThreadID)
{
	const uint2 localColorPos = dispatchID.xy * kAuditSampleStride + 1u;
	if (localColorPos.x >= ColorWidth || localColorPos.y >= ColorHeight ||
		ColorWidth == 0 || ColorHeight == 0 || DepthWidth == 0 || DepthHeight == 0) {
		return;
	}

	uint depthTexWidth, depthTexHeight;
	DepthIn.GetDimensions(depthTexWidth, depthTexHeight);
	const uint2 depthPos = uint2(
		(localColorPos.x * DepthWidth) / ColorWidth,
		(localColorPos.y * DepthHeight) / ColorHeight) +
		uint2(DepthOffsetX, DepthOffsetY);
	if (depthPos.x >= depthTexWidth || depthPos.y >= depthTexHeight)
		return;

	const bool robustClear =
		IsDilatedHiddenDepth(depthPos, uint2(depthTexWidth, depthTexHeight));
	bool candidateClear = robustClear;
	bool invalidMaskLookup = false;
	if (AuditCandidateMode == kAuditCandidateLegacySingleSample) {
		candidateClear = DepthIn[depthPos] == 0.0;
	} else if (AuditCandidateMode == kAuditCandidateReducedMask) {
		uint maskTexWidth, maskTexHeight;
		ReducedHMDMaskIn.GetDimensions(maskTexWidth, maskTexHeight);
		const uint2 maskPos = uint2(
			(localColorPos.x * AuditMaskWidth) / ColorWidth,
			(localColorPos.y * AuditMaskHeight) / ColorHeight);
		invalidMaskLookup =
			AuditMaskWidth == 0 || AuditMaskHeight == 0 ||
			maskPos.x >= AuditMaskWidth || maskPos.y >= AuditMaskHeight ||
			maskPos.x >= maskTexWidth || maskPos.y >= maskTexHeight;
		candidateClear = !invalidMaskLookup && ReducedHMDMaskIn[maskPos] != 0u;
	}

	uint ignored;
	HMDMaskAuditCounters.InterlockedAdd(0, 1u, ignored);
	if (robustClear)
		HMDMaskAuditCounters.InterlockedAdd(4, 1u, ignored);
	if (candidateClear)
		HMDMaskAuditCounters.InterlockedAdd(8, 1u, ignored);
	if (robustClear && !candidateClear)
		HMDMaskAuditCounters.InterlockedAdd(12, 1u, ignored);
	if (!robustClear && candidateClear)
		HMDMaskAuditCounters.InterlockedAdd(16, 1u, ignored);
	if (robustClear != candidateClear)
		HMDMaskAuditCounters.InterlockedAdd(20, 1u, ignored);
	if (invalidMaskLookup)
		HMDMaskAuditCounters.InterlockedAdd(24, 1u, ignored);
	if (dispatchID.x == 0 && dispatchID.y == 0)
		HMDMaskAuditCounters.InterlockedAdd(28, 1u, ignored);
}
