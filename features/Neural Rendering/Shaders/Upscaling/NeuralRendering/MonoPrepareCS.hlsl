#include "Upscaling/NeuralRendering/MonoCommon.hlsli"
Texture2D<float4> Source : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4> ModelColor : register(u0);
RWTexture2D<float> ModelDepth : register(u1);
RWTexture2D<float2> ModelMotion : register(u2);
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_NATIVE_CONTROL_MASK)
#	include "Upscaling/NeuralRendering/MonoSelection.hlsli"
RWTexture2D<unorm float4> ModelControlMask : register(u3);
#endif

#ifndef NR_PERIPHERAL_COMPRESSION
#	undef NR_FOOTPRINT_SUBDIVISIONS
#	define NR_FOOTPRINT_SUBDIVISIONS 1
#elif !defined(DEVBENCH_BRIDGE_ENABLED) || !defined(NR_FOOTPRINT_SUBDIVISIONS)
#	define NR_FOOTPRINT_SUBDIVISIONS 2
#endif
static const uint MONO_SOURCE_PATCH_COUNT = NR_FOOTPRINT_SUBDIVISIONS * NR_FOOTPRINT_SUBDIVISIONS;
struct MonoSourceFootprint
{
	float2 lower[MONO_SOURCE_PATCH_COUNT];
	float2 upper[MONO_SOURCE_PATCH_COUNT];
};

MonoSourceFootprint BuildMonoFootprint(float2 coordinate, float2 samplingSize, uint2 size, uint2 start, uint2 end, out float2 lower, out float2 upper)
{
	MonoSourceFootprint footprint;
	lower = float2(end);
	upper = float2(start);
	[unroll] for (uint patch = 0; patch < MONO_SOURCE_PATCH_COUNT; ++patch)
	{
		float2 position = coordinate * NR_FOOTPRINT_SUBDIVISIONS + float2(patch % NR_FOOTPRINT_SUBDIVISIONS, patch / NR_FOOTPRINT_SUBDIVISIONS);
		float2 patchLower = position / (samplingSize * NR_FOOTPRINT_SUBDIVISIONS);
		float2 patchUpper = (position + 1.0) / (samplingSize * NR_FOOTPRINT_SUBDIVISIONS);
		if (PeripheralProtected < 1.0)
			PeripheralSourceBounds(patchLower, patchUpper, patchLower, patchUpper);
		footprint.lower[patch] = max(saturate(patchLower) * size, float2(start));
		footprint.upper[patch] = min(saturate(patchUpper) * size, float2(end));
		lower = min(lower, footprint.lower[patch]);
		upper = max(upper, footprint.upper[patch]);
	}
	return footprint;
}
float FootprintWeight(MonoSourceFootprint footprint, uint2 pixel)
{
	float weight = 0.0;
	[unroll] for (uint patch = 0; patch < MONO_SOURCE_PATCH_COUNT; ++patch)
	{
		const float2 overlap = max(0.0, min(footprint.upper[patch], float2(pixel + 1u)) - max(footprint.lower[patch], float2(pixel)));
		weight += overlap.x * overlap.y;
	}
	return weight;
}

bool ResolveSampleLayout(uint2 pixel, uint2 grid, out float2 coordinate, out float2 samplingSize, out uint2 sourceStart, out uint2 sourceEnd, out int tile)
{
	coordinate = float2(pixel);
	samplingSize = float2(grid);
	sourceStart = 0u;
	sourceEnd = SourceSize;
	tile = -1;
	if (Packing.x > 0u) {
		tile = PackedTile(pixel);
		if (tile < 0)
			return false;
		coordinate = float2(pixel - TilePacked[tile].xy + TileModel[tile].xy);
		samplingSize = Packing.yz;
		sourceStart = TileSource[tile].xy;
		sourceEnd = sourceStart + TileSource[tile].zw;
	}
	return true;
}

void PrepareColor(uint2 pixel)
{
	float2 coordinate, samplingSize;
	uint2 sourceStart, sourceEnd;
	int tile;
	if (!ResolveSampleLayout(pixel, ModelSize, coordinate, samplingSize, sourceStart, sourceEnd, tile)) {
		ModelColor[pixel] = 0.0;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_NATIVE_CONTROL_MASK)
		ModelControlMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
#endif
		return;
	}
	float2 lower, upper;
	const MonoSourceFootprint footprint = BuildMonoFootprint(coordinate, samplingSize, SourceSize, sourceStart, sourceEnd, lower, upper);
	float4 color = 0.0;
	float area = 0.0;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_NATIVE_CONTROL_MASK)
	float selection = 0.0;
#endif
	// Area filtering keeps fractional inference scales independent of subpixel phase.
	[loop] for (uint y = uint(floor(lower.y)); y < uint(ceil(upper.y)); ++y)
	{
		[loop] for (uint x = uint(floor(lower.x)); x < uint(ceil(upper.x)); ++x)
		{
			const uint2 source = min(uint2(x, y), sourceEnd - 1u);
			const float weight = FootprintWeight(footprint, source);
			if (weight <= 0.0)
				continue;
			const float4 sampledColor = Source.Load(int3(source, 0));
			float3 prepared;
			const bool valid = ForwardColor(sampledColor.rgb, prepared) && all(prepared >= 0.0) && all(prepared <= 65504.0);
			color += float4(valid ? prepared : 0.0, isfinite(sampledColor.a) ? sampledColor.a : 1.0) * weight;
			area += weight;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_NATIVE_CONTROL_MASK)
			selection = max(selection, SelectionWeight(source) * ScreenWeight(source));
#endif
		}
	}
	ModelColor[pixel] = area > 0.0 ? color / area : 0.0;
#if defined(DEVBENCH_BRIDGE_ENABLED) && defined(NR_NATIVE_CONTROL_MASK)
	ModelControlMask[pixel] = float4(selection.xxx, 1.0);
#endif
}

void PrepareGuides(uint2 outputPixel)
{
	float2 coordinate, samplingSize;
	uint2 sourceStart, sourceEnd;
	int tile;
	if (!ResolveSampleLayout(outputPixel, ModelGuideSize, coordinate, samplingSize, sourceStart, sourceEnd, tile)) {
		ModelDepth[outputPixel] = 1.0;
		ModelMotion[outputPixel] = 0.0;
		return;
	}
	const uint2 guideStart = sourceStart * GuideSize / SourceSize;
	const uint2 guideLimit = (sourceEnd * GuideSize + SourceSize - 1u) / SourceSize;
	float2 guideLower, guideUpper;
	MonoSourceFootprint guideFootprint = BuildMonoFootprint(coordinate, samplingSize, GuideSize, guideStart, guideLimit, guideLower, guideUpper);
	[unroll] for (uint patch = 0; patch < MONO_SOURCE_PATCH_COUNT; ++patch)
	{
		guideFootprint.lower[patch] += Sampling.xy;
		guideFootprint.upper[patch] += Sampling.xy;
	}
	const int2 guideFirst = int2(floor(guideLower + Sampling.xy));
	const int2 guideEnd = int2(ceil(guideUpper + Sampling.xy));
	float closest = 1.0;
	float2 vector = 0.0;
	bool found = false;
	[loop] for (int y = guideFirst.y; y < guideEnd.y; ++y)
	{
		[loop] for (int x = guideFirst.x; x < guideEnd.x; ++x)
		{
			const int2 pixel = clamp(int2(x, y), 0, int2(GuideSize) - 1);
			// Guides use the same subdivided mapping as colour, on their own retained grid.
			if (PeripheralProtected < 1.0 && FootprintWeight(guideFootprint, uint2(pixel)) <= 0.0)
				continue;
			const float candidate = Depth.Load(int3(pixel, 0));
			const float2 velocity = Motion.Load(int3(pixel, 0));
			if (isfinite(candidate) && candidate >= 0.0 && candidate <= 1.0 && all(isfinite(velocity)) && (!found || candidate < closest)) {
				closest = candidate;
				vector = velocity;
				found = true;
			}
		}
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (Reconstruction.w < 0.5) {
		const float2 center = PeripheralProtected < 1.0 ? PeripheralInverse((coordinate + 0.5) / samplingSize) : (coordinate + 0.5) / samplingSize;
		const int2 centerPixel = int2(clamp(center * GuideSize + Sampling.xy, 0.0, float2(GuideSize - 1u)));
		const float2 centerMotion = Motion.Load(int3(centerPixel, 0));
		if (all(isfinite(centerMotion)))
			vector = centerMotion;
	}
#endif
	ModelDepth[outputPixel] = closest;
	float2 atlasMotion = vector * samplingSize / ModelGuideSize;
	if (PeripheralProtected < 1.0) {
		const float2 anchor = PeripheralInverse((float2(outputPixel) + 0.5) / ModelGuideSize);
		atlasMotion = PeripheralForward(anchor + vector) - PeripheralForward(anchor);
		if (!all(isfinite(atlasMotion)))
			atlasMotion = 2.0;
	}
	if (tile >= 0) {
		const float2 position = float2(outputPixel) + 0.5;
		const float2 reach = abs(atlasMotion) * ModelGuideSize + 1.0;
		// Packed actors retain matching grids; no lookup may cross actor history.
		if (any(position - reach < TilePacked[tile].xy) || any(position + reach >= TilePacked[tile].xy + TilePacked[tile].zw))
			atlasMotion = 2.0;
	}
	ModelMotion[outputPixel] = atlasMotion;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (all(id.xy < InferenceRegion.zw))
		PrepareColor(InferenceRegion.xy + id.xy);
	const uint2 guideStart = InferenceRegion.xy * ModelGuideSize / ModelSize;
	const uint2 guideEnd = ((InferenceRegion.xy + InferenceRegion.zw) * ModelGuideSize + ModelSize - 1u) / ModelSize;
	if (all(id.xy < guideEnd - guideStart))
		PrepareGuides(guideStart + id.xy);
}
