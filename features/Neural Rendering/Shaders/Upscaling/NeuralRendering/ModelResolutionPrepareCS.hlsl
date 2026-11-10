#include "Common/DepthOrder.hlsli"
#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"
#ifdef NR_PERIPHERAL_COMPRESSION
#	include "Upscaling/NeuralRendering/PeripheralCompression.hlsli"
#endif

Texture2D<float4> InputColor : register(t0);
Texture2D<float> InputDepth : register(t1);
Texture2D<float2> InputMotion : register(t2);
Texture2D<float> InputMask : register(t3);
RWTexture2D<float4> ProxyColor : register(u0);
RWTexture2D<float> ProxyDepth : register(u1);
RWTexture2D<float2> ProxyMotion : register(u2);
RWTexture2D<float> ProxyMask : register(u3);

void ModelSourceBounds(float2 pixel, float2 grid, uint2 size, uint2 offset, uint2 end,
	out float2 lower, out float2 upper)
{
#ifdef NR_PERIPHERAL_COMPRESSION
	PeripheralSourceBounds(pixel / grid, (pixel + 1.0) / grid, lower, upper);
	lower = clamp(lower * float2(size), float2(offset), float2(end));
	upper = clamp(upper * float2(size), float2(offset), float2(end));
	// Warped crop corners are context only; extend the owned edge instead of reading outside the eye.
	bool2 empty = upper <= lower;
	lower = float2(empty.x ? min(lower.x, float(end.x - 1u)) : lower.x,
		empty.y ? min(lower.y, float(end.y - 1u)) : lower.y);
	upper = float2(empty.x ? lower.x + 1.0 : upper.x, empty.y ? lower.y + 1.0 : upper.y);
#else
	lower = max(pixel * float2(size) / grid, float2(offset));
	upper = min((pixel + 1.0) * float2(size) / grid, float2(end));
#endif
}

#ifdef NR_PERIPHERAL_COMPRESSION
#	if !defined(DEVBENCH_BRIDGE_ENABLED) || !defined(NR_FOOTPRINT_SUBDIVISIONS)
#		define NR_FOOTPRINT_SUBDIVISIONS 2
#	endif
static const uint MODEL_SOURCE_PATCH_COUNT = NR_FOOTPRINT_SUBDIVISIONS * NR_FOOTPRINT_SUBDIVISIONS;
#else
static const uint MODEL_SOURCE_PATCH_COUNT = 1u;
#endif

struct ModelSourceFootprint
{
	float2 lower[MODEL_SOURCE_PATCH_COUNT];
	float2 upper[MODEL_SOURCE_PATCH_COUNT];
};

ModelSourceFootprint BuildModelSourceFootprint(float2 pixel, float2 grid, uint2 size, uint2 offset, uint2 end,
	out float2 lower, out float2 upper)
{
	ModelSourceFootprint footprint;
	lower = float2(end);
	upper = float2(offset);
	// Smaller inverse-cell bounds reduce corner contamination without fetching a source texel twice.
	[unroll] for (uint patch = 0; patch < MODEL_SOURCE_PATCH_COUNT; ++patch)
	{
		float2 position = pixel, patchGrid = grid;
#ifdef NR_PERIPHERAL_COMPRESSION
		position = pixel * NR_FOOTPRINT_SUBDIVISIONS + float2(patch % NR_FOOTPRINT_SUBDIVISIONS, patch / NR_FOOTPRINT_SUBDIVISIONS);
		patchGrid *= NR_FOOTPRINT_SUBDIVISIONS;
#endif
		ModelSourceBounds(position, patchGrid, size, offset, end, footprint.lower[patch], footprint.upper[patch]);
		lower = min(lower, footprint.lower[patch]);
		upper = max(upper, footprint.upper[patch]);
	}
	return footprint;
}

void ModelSourceRowWeights(ModelSourceFootprint footprint, uint y, out float weights[MODEL_SOURCE_PATCH_COUNT])
{
	[unroll] for (uint patch = 0; patch < MODEL_SOURCE_PATCH_COUNT; ++patch)
		weights[patch] = max(0.0, min(footprint.upper[patch].y, float(y + 1u)) - max(footprint.lower[patch].y, float(y)));
}

float ModelSourceWeight(ModelSourceFootprint footprint, float rowWeights[MODEL_SOURCE_PATCH_COUNT], uint x)
{
	float weight = 0.0;
	[unroll] for (uint patch = 0; patch < MODEL_SOURCE_PATCH_COUNT; ++patch)
		weight += rowWeights[patch] * max(0.0, min(footprint.upper[patch].x, float(x + 1u)) - max(footprint.lower[patch].x, float(x)));
	return weight;
}

struct GuideDonor
{
	float depth;
	float2 motion;
	uint2 pixel;
	bool found;
};

GuideDonor EmptyGuideDonor()
{
	GuideDonor donor;
	donor.depth = DepthOrder::Far();
	donor.motion = 0.0;
	donor.pixel = 0u;
	donor.found = false;
	return donor;
}

void ConsiderGuide(uint2 source, inout GuideDonor donor)
{
	float depth = InputDepth.Load(int3(source, 0));
	if (isfinite(depth) && depth >= 0.0 && depth <= 1.0 && (!donor.found || DepthOrder::IsNearer(depth, donor.depth))) {
		float2 velocity = InputMotion.Load(int3(source, 0));
		if (all(isfinite(velocity))) {
			donor.depth = depth;
			donor.motion = velocity;
#ifdef NR_PERIPHERAL_COMPRESSION
			donor.pixel = source;
#endif
			donor.found = true;
		}
	}
}

float2 GuideMotion(GuideDonor donor, uint2 pixel, uint2 guideSize, uint2 modelGuideSize)
{
	float2 atlasMotion = donor.motion * MotionVectorScale;
#ifdef NR_PERIPHERAL_COMPRESSION
	// Motion is anchored to the winning depth sample in the shared or independent traversal.
	float2 anchor = (float2(donor.pixel) + 0.5) / float2(guideSize);
#	if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_CELL_CENTRE_MOTION)
	anchor = PeripheralInverse((float2(pixel) + 0.5) / float2(modelGuideSize));
#	endif
	atlasMotion = PeripheralForward(anchor + atlasMotion) - PeripheralForward(anchor);
	atlasMotion = all(isfinite(atlasMotion)) ? atlasMotion : 0.0;
#endif
	return atlasMotion;
}

void PrepareColor(uint2 id, bool sharedGuides)
{
	if (any(id >= ModelRegionSize))
		return;
	uint2 pixel = ModelRegionOffset + id;
	float2 samplePixel = float2(pixel), sampleGrid = float2(ModelSize);
	uint2 sourceOffset = SourceRegionOffset, sourceEnd = SourceRegionOffset + SourceRegionSize;
	if (PackedCount != 0u) {
		uint tileIndex = FindPackedCrop(float2(pixel) + 0.5, false);
		if (tileIndex == PackedCount) {
			ProxyColor[pixel] = 0.0;
			if (HasMask != 0u)
				ProxyMask[pixel] = 0.0;
			return;
		}
		PackedCropTile tile = PackedTiles[tileIndex];
		samplePixel += float2(tile.model.xy) - float2(tile.packed.xy);
		sampleGrid = float2(UnpackedSize);
		sourceOffset = tile.source.xy;
		sourceEnd = tile.source.xy + tile.source.zw;
	}
	float2 lower, upper;
	ModelSourceFootprint footprint = BuildModelSourceFootprint(samplePixel, sampleGrid, SourceSize, sourceOffset, sourceEnd, lower, upper);
	uint2 first = uint2(floor(lower));
	uint2 end = uint2(ceil(upper));
	float4 color = 0.0;
	float area = 0.0, mask = 0.0;
	GuideDonor donor = EmptyGuideDonor();
	// An area filter avoids phase-dependent shimmer when the model grid changes size.
	[loop] for (uint y = first.y; y < end.y; ++y)
	{
		float rowWeights[MODEL_SOURCE_PATCH_COUNT];
		ModelSourceRowWeights(footprint, y, rowWeights);
		[loop] for (uint x = first.x; x < end.x; ++x)
		{
			uint2 source = min(uint2(x, y), sourceEnd - 1u);
			float weight = ModelSourceWeight(footprint, rowWeights, source.x);
			if (weight <= 0.0)
				continue;
			if (sharedGuides)
				ConsiderGuide(source, donor);
			float4 value = InputColor.Load(int3(source, 0));
			color += (all(isfinite(value)) ? value : 0.0) * weight;
			area += weight;
			if (HasMask != 0u) {
				float selection = InputMask.Load(int3(source, 0));
				mask = max(mask, isfinite(selection) ? saturate(selection) : 0.0);
			}
		}
	}
	if (sharedGuides) {
		ProxyDepth[pixel] = donor.depth;
		ProxyMotion[pixel] = GuideMotion(donor, pixel, SourceSize, ModelSize);
	}
	ProxyColor[pixel] = area > 0.0 ? color / area : 0.0;
	if (HasMask != 0u)
		ProxyMask[pixel] = mask;
}

void PrepareGuides(uint2 id)
{
	uint2 guideSize, modelGuideSize;
	InputDepth.GetDimensions(guideSize.x, guideSize.y);
	ProxyDepth.GetDimensions(modelGuideSize.x, modelGuideSize.y);
	uint2 regionOffset = ModelRegionOffset * modelGuideSize / ModelSize;
	uint2 regionEnd = ((ModelRegionOffset + ModelRegionSize) * modelGuideSize + ModelSize - 1u) / ModelSize;
	if (any(id >= regionEnd - regionOffset))
		return;
	uint2 sourceOffset = SourceRegionOffset * guideSize / SourceSize;
	uint2 sourceEnd = ((SourceRegionOffset + SourceRegionSize) * guideSize + SourceSize - 1u) / SourceSize;
	uint2 pixel = regionOffset + id;
	float2 lower, upper;
#ifdef NR_PERIPHERAL_COMPRESSION
	ModelSourceFootprint footprint = BuildModelSourceFootprint(float2(pixel), float2(modelGuideSize), guideSize, sourceOffset, sourceEnd, lower, upper);
#else
	ModelSourceBounds(float2(pixel), float2(modelGuideSize), guideSize, sourceOffset, sourceEnd, lower, upper);
#endif
	uint packedTileIndex = PackedCount;
	if (PackedCount != 0u) {
		float2 atlasPosition = (float2(pixel) + 0.5) * float2(ModelSize) / float2(modelGuideSize);
		uint tileIndex = FindPackedCrop(atlasPosition, false);
		if (tileIndex == PackedCount) {
			ProxyDepth[pixel] = DepthOrder::Far();
			ProxyMotion[pixel] = 0.0;
			return;
		}
		packedTileIndex = tileIndex;
		PackedCropTile tile = PackedTiles[tileIndex];
		float2 atlasLower = max(float2(pixel) * float2(ModelSize) / float2(modelGuideSize), float2(tile.packed.xy));
		float2 atlasUpper = min(float2(pixel + 1u) * float2(ModelSize) / float2(modelGuideSize), float2(tile.packed.xy + tile.packed.zw));
		float2 shift = float2(tile.model.xy) - float2(tile.packed.xy);
		lower = max((atlasLower + shift) * float2(guideSize) / float2(UnpackedSize), float2(tile.source.xy) * float2(guideSize) / float2(SourceSize));
		upper = min((atlasUpper + shift) * float2(guideSize) / float2(UnpackedSize), float2(tile.source.xy + tile.source.zw) * float2(guideSize) / float2(SourceSize));
	}
	uint2 first = min(uint2(floor(lower)), guideSize - 1u);
	uint2 end = min(uint2(ceil(upper)), guideSize);
	GuideDonor donor = EmptyGuideDonor();
	// Guide and colour grids retain their own resolutions and matching depth/motion samples.
	[loop] for (uint y = first.y; y < end.y; ++y)
	{
#ifdef NR_PERIPHERAL_COMPRESSION
		float rowWeights[MODEL_SOURCE_PATCH_COUNT];
		ModelSourceRowWeights(footprint, y, rowWeights);
#endif
		[loop] for (uint x = first.x; x < end.x; ++x)
		{
#ifdef NR_PERIPHERAL_COMPRESSION
			// Guides must not select foreground from corners excluded by the colour footprint.
			if (ModelSourceWeight(footprint, rowWeights, x) <= 0.0)
				continue;
#endif
			ConsiderGuide(uint2(x, y), donor);
		}
	}
	ProxyDepth[pixel] = donor.depth;
	float2 atlasMotion = GuideMotion(donor, pixel, guideSize, modelGuideSize);
	if (packedTileIndex < PackedCount) {
		PackedCropTile tile = PackedTiles[packedTileIndex];
		float2 position = (float2(pixel) + 0.5) * float2(ModelSize) / float2(modelGuideSize);
		float2 reach = abs(atlasMotion) * float2(ModelSize) + 1.0;
		// A temporal lookup crossing a tile must never sample an unrelated actor history.
		if (any(position - reach < float2(tile.packed.xy)) || any(position + reach >= float2(tile.packed.xy + tile.packed.zw)))
			atlasMotion = 2.0;
	}
	ProxyMotion[pixel] = atlasMotion;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
#ifdef NR_SHARED_PREPARATION
	// The retained CPU plan proves identical full-view source/model grids and regions.
	PrepareColor(id.xy, true);
#else
	PrepareColor(id.xy, false);
	PrepareGuides(id.xy);
#endif
}
