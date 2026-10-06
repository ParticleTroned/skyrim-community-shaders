#include "Common/CharacterCategoryMask.hlsli"

static const uint EligibilityRegionCapacity = 16u;

// Resolves the frozen active-stereo provenance tuple into one exact eye-local
// CSX output-selection mask. The CPU unions these same eligibility rectangles
// into the one Feature 18 compute subrect; this shader remains authoritative for
// exact per-pixel material compositing inside that conservative rectangle.

cbuffer CharacterMaskCB : register(b0)
{
	uint4 OutputAndSourceSize;                                // output width/height, source eye width/height
	uint4 SourceCrop;                                         // source eye base X, input min X/Y, input width
	uint4 Options;                                            // input height, eligibility count, feather radius, depth-aware feather
	float4 FeatherOptions;                                    // feather depth, test mode, diagnostics sample, weapons strength
	float4 VisibilityOptions;                                 // rejection threshold, visibility enabled, max distance, distance fade (game units)
	float4 DepthLinearization;                                // far, near, far-near, far*near
	row_major float4x4 CameraProjInverse;                     // current per-eye projection inverse
	float4 Jitter;                                            // current low-resolution pixel jitter in xy
	float4 CategoryStrengths;                                 // face, skin, hair, armour
	float4 EligibilityRectangles[EligibilityRegionCapacity];  // eye-local output pixels, min.xy/max.xy
	uint4 DispatchRegion;                                     // output-local offset.xy and dispatch extent.zw
	uint4 AuthoredRegion;                                     // valid current-frame full-eye source offset.xy/extent.zw

#ifdef GPU_CHARACTER_SUPPORT
	uint4 SupportGrid;  // source tile columns/rows, eye tile base, GPU support enabled
#endif
};

Texture2D<unorm float2> AuthoredTuple : register(t0);
Texture2D<float> AuthoredDepth : register(t1);
Texture2D<float> CurrentDepth : register(t2);
RWTexture2D<unorm float> CharacterSelectionMask : register(u0);
RWByteAddressBuffer DiagnosticCounters : register(u1);
static const uint DiagnosticCounterCount = 13u;
groupshared uint GroupCounters[DiagnosticCounterCount];
#ifdef GPU_CHARACTER_SUPPORT
StructuredBuffer<uint4> CurrentSupport : register(t3);
RWTexture2D<uint> DirtyTiles : register(u2);
groupshared uint GroupSupport;
groupshared uint GroupPreviousDirty;
groupshared uint GroupCurrentDirty;

int2 GetGlobalSourcePixel(int2 localSourcePixel);

// Bounds include every clamped bilinear/feather tap. Later depth and distance
// rejection can only remove coverage from this current-source category superset.
bool GroupHasSupport(uint2 origin)
{
	if (!all(isfinite(Jitter.xy)))
		return true;
	const uint2 last = min(origin + 7u, OutputAndSourceSize.xy - 1u);
	const float2 scale = float2(SourceCrop.w, Options.x) / float2(OutputAndSourceSize.xy);
	const int radius = Options.w != 0u ? min(int(Options.z), 4) : 0;
	const int2 lower = int2(floor((float2(origin) + 0.5) * scale - 0.5 - Jitter.xy)) - radius;
	const int2 upper = int2(floor((float2(last) + 0.5) * scale - 0.5 - Jitter.xy)) + radius + 1;
	const int2 minimum = GetGlobalSourcePixel(lower) - int2(SourceCrop.x, 0);
	const int2 maximum = GetGlobalSourcePixel(upper) - int2(SourceCrop.x, 0) + 1;
	const uint2 firstTile = uint2(minimum) / 32u;
	const uint2 lastTile = uint2(maximum - 1) / 32u;
	// Invalid metadata or an unusually large footprint keeps the reference work.
	if (any(lastTile >= SupportGrid.xy) || any(lastTile - firstTile > 3u))
		return true;
	[loop] for (uint y = firstTile.y; y <= lastTile.y; ++y)
	{
		[loop] for (uint x = firstTile.x; x <= lastTile.x; ++x)
		{
			const uint4 bounds = CurrentSupport[SupportGrid.z + y * SupportGrid.x + x];
			if (all(bounds.zw > bounds.xy) && all(int2(bounds.xy) < maximum) && all(int2(bounds.zw) > minimum))
				return true;
		}
	}
	return false;
}

#endif

static const uint MaskPixels = 0u;
static const uint AuthoredFacePixels = 1u;
static const uint AuthoredSkinPixels = 2u;
static const uint AuthoredHairPixels = 3u;
static const uint VisibleFacePixels = 4u;
static const uint VisibleSkinPixels = 5u;
static const uint VisibleHairPixels = 6u;
static const uint VisibilityRejectedPixels = 7u;
static const uint DistanceRejectedPixels = 8u;
static const uint AuthoredArmorPixels = 9u;
static const uint VisibleArmorPixels = 11u;

bool IsInsideEligibilityRegion(float2 pixelCenter)
{
	[loop] for (uint index = 0;
		index < min(Options.y, EligibilityRegionCapacity); ++index)
	{
		const float4 region = EligibilityRectangles[index];
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

float2 ReadAuthoredTuple(int2 localSourcePixel)
{
	const int2 sourcePixel = GetGlobalSourcePixel(localSourcePixel);
	const int2 eyePixel = sourcePixel - int2(SourceCrop.x, 0);
	if (any(eyePixel < int2(AuthoredRegion.xy)) ||
		any(eyePixel >= int2(AuthoredRegion.xy + AuthoredRegion.zw)))
		return float2(0.0, 0.0);
	return AuthoredTuple.Load(int3(sourcePixel, 0));
}

uint ReadAuthoredCategory(int2 localSourcePixel)
{
	return CharacterCategoryMask::DecodeCategory(ReadAuthoredTuple(localSourcePixel));
}

float GetCategoryStrength(uint category)
{
	if (category == 1u)
		return CategoryStrengths.x;
	if (category == 2u)
		return CategoryStrengths.y;
	if (category == 3u)
		return CategoryStrengths.z;
	if (category == 4u)
		return CategoryStrengths.w;
	if (category == 5u)
		return FeatherOptions.w;
	return 0.0;
}

float ReadAuthoredDepth(int2 localSourcePixel)
{
	const int2 eyePixel = GetGlobalSourcePixel(localSourcePixel) - int2(SourceCrop.x, 0);
	if (any(eyePixel < int2(AuthoredRegion.xy)) ||
		any(eyePixel >= int2(AuthoredRegion.xy + AuthoredRegion.zw)))
		return 1.0;
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

bool IsAuthoredSurfaceVisible(
	int2 localSourcePixel,
	out float currentDepth,
	out float authoredRawDepth)
{
	const bool visibilityRejectionEnabled = VisibilityOptions.y >= 0.5;
	const bool depthFeatherEnabled = Options.w != 0 && Options.z != 0;
	const bool distanceCullEnabled = VisibilityOptions.z > 0.0;
	if (!visibilityRejectionEnabled && !depthFeatherEnabled && !distanceCullEnabled) {
		currentDepth = 0.0;
		authoredRawDepth = 0.0;
		return true;
	}

	currentDepth = 0.0;
	authoredRawDepth = 0.0;
	if (visibilityRejectionEnabled || depthFeatherEnabled)
		currentDepth = LinearizeDepth(ReadCurrentDepth(localSourcePixel));
	if (visibilityRejectionEnabled || distanceCullEnabled)
		authoredRawDepth = ReadAuthoredDepth(localSourcePixel);
	if (!visibilityRejectionEnabled)
		return true;

	const float authoredDepth = LinearizeDepth(authoredRawDepth);
	const float tolerance = max(
		1.0,
		max(abs(authoredDepth), abs(currentDepth)) * VisibilityOptions.x);
	// Later depth may legitimately be farther (cleared or transparent). Reject
	// only a materially closer surface that now occludes the authored category.
	return currentDepth + tolerance >= authoredDepth;
}

float ReconstructEyeDistance(int2 localSourcePixel, float rawDepth)
{
	const int2 inputSize = int2(SourceCrop.w, Options.x);
	localSourcePixel = clamp(localSourcePixel, int2(0, 0), inputSize - 1);
	const float2 fullEyePixel =
		float2(int2(SourceCrop.y, SourceCrop.z) + localSourcePixel) + 0.5;
	const float2 uv = fullEyePixel / float2(OutputAndSourceSize.zw);
	const float4 clipPosition = float4(
		uv.x * 2.0 - 1.0,
		1.0 - uv.y * 2.0,
		rawDepth,
		1.0);
	const float4 viewPosition = mul(CameraProjInverse, clipPosition);
	return abs(viewPosition.w) > 1.0e-6 ?
	           length(viewPosition.xyz / viewPosition.w) :
	           3.402823466e+38;
}

float GetDistanceWeight(int2 localSourcePixel, float rawDepth)
{
	if (VisibilityOptions.z <= 0.0)
		return 1.0;
	const float eyeDistance = ReconstructEyeDistance(localSourcePixel, rawDepth);
	const float fadeWidth = min(
		VisibilityOptions.z,
		max(VisibilityOptions.w, 1.0e-6));
	return saturate((VisibilityOptions.z - eyeDistance) / fadeWidth);
}

// Reconstruct coverage, never interpolate encoded categorical IDs. All samples
// belong to this source frame; no stale temporal mask can ghost onto an occluder.
float ReconstructCoverage(float2 sourcePosition, uint centerCategory,
	float referenceDepth)
{
	if (centerCategory != 0u && GetCategoryStrength(centerCategory) <= 0.0)
		return 0.0;
	const int2 basePixel = int2(floor(sourcePosition));
	const float2 fraction = frac(sourcePosition);
	float mask = 0.0;
	[unroll] for (int y = 0; y < 2; ++y)
	{
		[unroll] for (int x = 0; x < 2; ++x)
		{
			const float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) *
			                     (y != 0 ? fraction.y : 1.0 - fraction.y);
			if (weight <= 0.0)
				continue;
			const int2 samplePixel = basePixel + int2(x, y);
			const float2 tuple = ReadAuthoredTuple(samplePixel);
			const uint category = CharacterCategoryMask::DecodeCategory(tuple);
			const float strength = GetCategoryStrength(category) * CharacterCategoryMask::DecodeFocusWeight(tuple);
			if (strength <= 0.0)
				continue;
			float currentDepth, authoredRawDepth;
			if (!IsAuthoredSurfaceVisible(samplePixel, currentDepth, authoredRawDepth))
				continue;
			if (VisibilityOptions.y >= 0.5) {
				const float authoredDepth = LinearizeDepth(authoredRawDepth);
				const float tolerance = max(1.0,
					max(abs(referenceDepth), abs(authoredDepth)) * VisibilityOptions.x);
				if (authoredDepth > referenceDepth + tolerance)
					continue;
			}
			mask += weight * strength * GetDistanceWeight(samplePixel, authoredRawDepth);
		}
	}
	return mask;
}

void CountCategory(uint category, uint firstCounter)
{
	if (category >= 1u && category <= CharacterCategoryMask::CategoryCount) {
		uint ignored;
		const uint counter = category <= 3u ? firstCounter + category - 1u :
		                                      (firstCounter == AuthoredFacePixels ? AuthoredArmorPixels : VisibleArmorPixels) + category - 4u;
		InterlockedAdd(GroupCounters[counter], 1u, ignored);
	}
}

[numthreads(8, 8, 1)] void main(
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint3 groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex) {
#ifdef NR_DIAGNOSTICS
	const bool measureCoverage = FeatherOptions.z > 0.5;
#else
	const bool measureCoverage = false;
#endif
	const bool insideDispatch = all(dispatchThreadId.xy < DispatchRegion.zw);
	const uint2 outputPixelId = dispatchThreadId.xy + DispatchRegion.xy;

#ifdef GPU_CHARACTER_SUPPORT
	const bool useSupport = SupportGrid.w != 0u;
	if (useSupport) {
		if (groupIndex == 0u) {
			GroupSupport = GroupHasSupport(groupId.xy * 8u) ? 1u : 0u;
			GroupPreviousDirty = DirtyTiles[groupId.xy];
			GroupCurrentDirty = 0u;
		}
		GroupMemoryBarrierWithGroupSync();
		if (GroupSupport == 0u) {
			if (GroupPreviousDirty != 0u && all(outputPixelId < OutputAndSourceSize.xy))
				CharacterSelectionMask[outputPixelId] = 0.0;
			if (groupIndex == 0u)
				DirtyTiles[groupId.xy] = 0u;
			return;
		}
	}

#endif
	if (measureCoverage) {
		if (groupIndex < DiagnosticCounterCount)
			GroupCounters[groupIndex] = 0u;
		GroupMemoryBarrierWithGroupSync();
	}
	const uint2 inputSize = uint2(SourceCrop.w, Options.x);
	if (measureCoverage && insideDispatch && all(outputPixelId < inputSize)) {
		CountCategory(
			ReadAuthoredCategory(int2(outputPixelId)),
			AuthoredFacePixels);
	}

	const uint2 outputSize = OutputAndSourceSize.xy;
	if (insideDispatch && all(outputPixelId < outputSize)) {
		const float2 outputPixel = float2(outputPixelId) + 0.5;
		const float2 outputUv = outputPixel / float2(outputSize);
		const float2 sourcePosition =
			outputUv * float2(SourceCrop.w, Options.x) - 0.5 - Jitter.xy;
		const int2 sourcePixel = int2(floor(sourcePosition + 0.5));
		float mask = 0.0;
		if (IsInsideEligibilityRegion(outputPixel)) {
			const float2 centerTuple = ReadAuthoredTuple(sourcePixel);
			const uint centerCategory = CharacterCategoryMask::DecodeCategory(centerTuple);
			const float centerStrength = GetCategoryStrength(centerCategory) * CharacterCategoryMask::DecodeFocusWeight(centerTuple);
			float centerDepth = 0.0;
			float centerAuthoredRawDepth = 0.0;
			const bool centerVisible =
				IsAuthoredSurfaceVisible(
					sourcePixel, centerDepth, centerAuthoredRawDepth);
			const float centerDistanceWeight =
				GetDistanceWeight(sourcePixel, centerAuthoredRawDepth);
			const bool centerWithinDistance = centerDistanceWeight > 0.0;
			// Background depth must not erase a nearby character's subpixel coverage.
			// Each contributing category sample checks visibility and distance itself.
			const bool centerEligible = centerCategory == 0u ||
			                            (centerVisible && centerWithinDistance);
			if (measureCoverage) {
				if (centerEligible)
					CountCategory(centerCategory, VisibleFacePixels);
				else if (centerCategory >= 1u && centerCategory <= CharacterCategoryMask::CategoryCount) {
					uint ignored;
					if (!centerVisible) {
						InterlockedAdd(
							GroupCounters[VisibilityRejectedPixels], 1u, ignored);
					} else if (!centerWithinDistance) {
						InterlockedAdd(
							GroupCounters[DistanceRejectedPixels], 1u, ignored);
					}
				}
			}
			mask = centerEligible ? ReconstructCoverage(
										sourcePosition, centerCategory, centerDepth) :
			                        0.0;
			// Keep the exact center-surface cull/fade authoritative at the edge.
			if (centerCategory != 0u)
				mask = min(mask, centerStrength * centerDistanceWeight);

			const float featherCeiling = centerCategory != 0u ?
			                                 centerStrength * centerDistanceWeight :
			                                 max(CategoryStrengths.x, max(CategoryStrengths.y, CategoryStrengths.z));
			if (centerEligible && mask < featherCeiling && Options.w != 0 && Options.z != 0 &&
				(centerCategory == 0u || GetCategoryStrength(centerCategory) > 0.0)) {
				const int radius = min(int(Options.z), 4);
				const int2 basePixel = int2(floor(sourcePosition));
				// Include the complete continuous kernel; entering/leaving samples have
				// zero weight instead of jumping when the nearest source texel changes.
				[loop] for (int y = -radius; y <= radius + 1; ++y)
				{
					[loop] for (int x = -radius; x <= radius + 1; ++x)
					{
						const int2 samplePixel = basePixel + int2(x, y);
						const float spatialWeight = saturate(1.0 -
															 length(float2(samplePixel) - sourcePosition) / float(radius + 1));
						if (spatialWeight <= 0.0)
							continue;
						const float2 neighborTuple = ReadAuthoredTuple(samplePixel);
						const uint neighborCategory = CharacterCategoryMask::DecodeCategory(neighborTuple);
						const float neighborStrength = GetCategoryStrength(neighborCategory) * CharacterCategoryMask::DecodeFocusWeight(neighborTuple);
						// Preserve category toggles inside character geometry while
						// allowing an enabled category to feather into background.
						if (neighborStrength <= 0.0 ||
							(centerCategory != 0u && neighborCategory != centerCategory))
							continue;
						float neighborDepth = 0.0;
						float neighborAuthoredRawDepth = 0.0;
						if (!IsAuthoredSurfaceVisible(
								samplePixel, neighborDepth,
								neighborAuthoredRawDepth))
							continue;
						const float neighborDistanceWeight = GetDistanceWeight(
							samplePixel, neighborAuthoredRawDepth);
						if (neighborDistanceWeight <= 0.0)
							continue;
						const float depthTolerance = max(
							1.0,
							max(centerDepth, neighborDepth) * FeatherOptions.x);
						if (abs(neighborDepth - centerDepth) > depthTolerance)
							continue;
						mask = max(mask,
							neighborStrength * spatialWeight * neighborDistanceWeight);
					}
				}
			}
			if (centerCategory != 0u)
				mask = min(mask, centerStrength * centerDistanceWeight);
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
		CharacterSelectionMask[outputPixelId] = mask;

#ifdef GPU_CHARACTER_SUPPORT
		if (useSupport && mask > (0.5 / 255.0)) {
			uint ignored;
			InterlockedOr(GroupCurrentDirty, 1u, ignored);
		}

#endif
		if (measureCoverage && mask > (0.5 / 255.0)) {
			uint ignored;
			InterlockedAdd(GroupCounters[MaskPixels], 1u, ignored);
		}
	}

#ifdef GPU_CHARACTER_SUPPORT
	if (useSupport) {
		GroupMemoryBarrierWithGroupSync();
		if (groupIndex == 0u)
			DirtyTiles[groupId.xy] = GroupCurrentDirty;
	}

#endif
	if (measureCoverage) {
		GroupMemoryBarrierWithGroupSync();
		if (groupIndex < DiagnosticCounterCount && GroupCounters[groupIndex] != 0u) {
			uint ignored;
			DiagnosticCounters.InterlockedAdd(
				groupIndex * 4u, GroupCounters[groupIndex], ignored);
		}
	}
}
