#include "Common/CharacterCategoryMask.hlsli"

// Resolves the frozen combined-stereo provenance tuple into one exact eye-local
// Feature 18 control mask. Eligibility rectangles are visual-mask policy only;
// they do not claim or request reduced provider inference dimensions.

cbuffer CharacterMaskCB : register(b0)
{
	uint4 OutputAndSourceSize;  // output width/height, source eye width/height
	uint4 SourceCrop;           // source eye base X, input min X/Y, input width
	uint4 Options;              // input height, ROI count, feather radius, depth-aware feather
	float4 FeatherOptions;      // feather depth, test mode, coverage sample, visibility depth
	float4 DepthLinearization;  // far, near, far-near, far*near
	float4 Jitter;              // current low-resolution pixel jitter in xy
	float4 CategoryStrengths;   // face, skin, hair, reserved
	float4 RoiRectangles[4];    // eye-local output pixels, min.xy/max.xy
};

Texture2D<unorm float4> AuthoredTuple : register(t0);
Texture2D<float> AuthoredDepth : register(t1);
Texture2D<float> CurrentDepth : register(t2);
RWTexture2D<unorm float> ControlMask : register(u0);
RWByteAddressBuffer CoverageCounter : register(u1);
groupshared uint GroupCoverage;

bool IsInsideEligibilityRegion(float2 pixelCenter)
{
	[unroll] for (uint index = 0; index < 4; ++index)
	{
		if (index >= Options.y)
			break;
		const float4 region = RoiRectangles[index];
		if (all(pixelCenter >= region.xy) && all(pixelCenter < region.zw))
			return true;
	}
	return false;
}

int2 GetGlobalSourcePixel(int2 localSourcePixel)
{
	const int2 inputSize = int2(SourceCrop.w, Options.x);
	localSourcePixel = clamp(localSourcePixel, int2(0, 0), inputSize - 1);
	return int2(SourceCrop.x + SourceCrop.y, SourceCrop.z) + localSourcePixel;
}

uint ReadAuthoredCategory(int2 localSourcePixel)
{
	const int2 sourcePixel = GetGlobalSourcePixel(localSourcePixel);
	return CharacterCategoryMask::DecodeCategory(
		AuthoredTuple.Load(int3(sourcePixel, 0)));
}

float GetCategoryStrength(uint category)
{
	if (category == 1u)
		return CategoryStrengths.x;
	if (category == 2u)
		return CategoryStrengths.y;
	if (category == 3u)
		return CategoryStrengths.z;
	return 0.0;
}

float ReadAuthoredDepth(int2 localSourcePixel)
{
	return AuthoredDepth.Load(
		int3(GetGlobalSourcePixel(localSourcePixel), 0));
}

float ReadCurrentDepth(int2 localSourcePixel)
{
	const int2 inputSize = int2(SourceCrop.w, Options.x);
	localSourcePixel = clamp(localSourcePixel, int2(0, 0), inputSize - 1);
	return CurrentDepth.Load(int3(localSourcePixel, 0));
}

float LinearizeDepth(float rawDepth)
{
	return DepthLinearization.w /
	       max(-rawDepth * DepthLinearization.z + DepthLinearization.x, 1.0e-6);
}

bool IsAuthoredSurfaceVisible(int2 localSourcePixel, out float currentDepth)
{
	const float authoredDepth = LinearizeDepth(ReadAuthoredDepth(localSourcePixel));
	currentDepth = LinearizeDepth(ReadCurrentDepth(localSourcePixel));
	const float tolerance = max(
		1.0,
		max(abs(authoredDepth), abs(currentDepth)) * FeatherOptions.w);
	return abs(authoredDepth - currentDepth) <= tolerance;
}

[numthreads(8, 8, 1)] void main(
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupIndex) {
	const bool measureCoverage = FeatherOptions.z > 0.5;
	if (measureCoverage) {
		if (groupIndex == 0)
			GroupCoverage = 0;
		GroupMemoryBarrierWithGroupSync();
	}

	const uint2 outputSize = OutputAndSourceSize.xy;
	if (all(dispatchThreadId.xy < outputSize)) {
		const float2 outputPixel = float2(dispatchThreadId.xy) + 0.5;
		float mask = 0.0;
		if (IsInsideEligibilityRegion(outputPixel)) {
			const float2 outputUv = outputPixel / float2(outputSize);
			const float2 sourcePosition =
				outputUv * float2(SourceCrop.w, Options.x) - 0.5 - Jitter.xy;
			const int2 sourcePixel = int2(floor(sourcePosition + 0.5));
			const uint centerCategory = ReadAuthoredCategory(sourcePixel);
			float centerDepth = 0.0;
			const bool centerVisible =
				IsAuthoredSurfaceVisible(sourcePixel, centerDepth);
			mask = centerVisible ? GetCategoryStrength(centerCategory) : 0.0;

			if (centerVisible && Options.w != 0 && Options.z != 0 && mask < 1.0) {
				const int radius = min(int(Options.z), 4);
				[loop] for (int y = -radius; y <= radius; ++y)
				{
					[loop] for (int x = -radius; x <= radius; ++x)
					{
						const int2 offset = int2(x, y);
						const uint neighborCategory =
							ReadAuthoredCategory(sourcePixel + offset);
						// Preserve category toggles inside character geometry while
						// allowing an enabled category to feather into background.
						if (neighborCategory == 0u ||
							(centerCategory != 0u && neighborCategory != centerCategory))
							continue;
						float neighborDepth = 0.0;
						if (!IsAuthoredSurfaceVisible(
								sourcePixel + offset, neighborDepth))
							continue;
						const float depthTolerance = max(
							1.0,
							max(centerDepth, neighborDepth) * FeatherOptions.x);
						if (abs(neighborDepth - centerDepth) > depthTolerance)
							continue;
						const float distanceWeight =
							saturate(1.0 - length(float2(offset)) / float(radius + 1));
						mask = max(
							mask,
							GetCategoryStrength(neighborCategory) * distanceWeight);
					}
				}
			}
		}

		const uint testMode = uint(FeatherOptions.y + 0.5);
		if (testMode == 1)
			mask = 0.0;
		else if (testMode == 2)
			mask = 1.0;
		else if (testMode == 3)
			mask = 0.5;
		else if (testMode == 4)
			mask = 1.0 - mask;
		mask = saturate(mask);
		ControlMask[dispatchThreadId.xy] = mask;
		if (measureCoverage && mask > (0.5 / 255.0)) {
			uint ignored;
			InterlockedAdd(GroupCoverage, 1, ignored);
		}
	}

	if (measureCoverage) {
		GroupMemoryBarrierWithGroupSync();
		if (groupIndex == 0 && GroupCoverage != 0) {
			uint ignored;
			CoverageCounter.InterlockedAdd(0, GroupCoverage, ignored);
		}
	}
}
