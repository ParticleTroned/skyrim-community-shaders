#ifndef NR_MONO_COMMON
#define NR_MONO_COMMON
#include "Upscaling/NeuralRendering/ColorCommon.hlsli"
#include "Upscaling/NeuralRendering/PeripheralCompression.hlsli"
#include "Common/CharacterCategoryMask.hlsli"
#include "Common/NeuralRenderingVisibility.hlsli"
cbuffer MonoConfiguration : register(b1)
{
	uint2 SourceSize;
	uint2 ModelSize;
	uint2 GuideSize;
	uint2 CategorySize;
	float4 ScreenRegion;
	float4 CategoryStrengths;
	float4 Selection;
	float4 DepthLinearization;
	float4 Sampling;
	uint4 Options;
	uint4 InferenceRegion;
};
cbuffer PackedConfiguration : register(b2)
{
	uint4 Packing;
	uint4 TileSource[4];
	uint4 TileModel[4];
	uint4 TilePacked[4];
};
bool ContainsPixel(uint4 rectangle, uint2 pixel)
{
	return all(pixel >= rectangle.xy) && all(pixel - rectangle.xy < rectangle.zw);
}
int SourceTile(uint2 pixel)
{
	[unroll] for (uint i = 0; i < 4; ++i)
		if (i < Packing.x && ContainsPixel(TileSource[i], pixel)) return int(i);
	return -1;
}
int PackedTile(uint2 pixel)
{
	[unroll] for (uint i = 0; i < 4; ++i)
		if (i < Packing.x && ContainsPixel(TilePacked[i], pixel)) return int(i);
	return -1;
}
float ScreenWeight(float2 pixel)
{
	if (ScreenRegion.x >= 1.0) return 1.0;
	const float2 uv = (pixel + 0.5) / SourceSize;
	const float2 d = abs(uv - (0.5 + ScreenRegion.zw)) * 2.0 / (ScreenRegion.x * float2(ScreenRegion.y, 1.0));
	const float radius = pow(dot(d * d, d * d), 0.25);
	const float feather = max(Selection.z / max(1.0, min(SourceSize.x * ScreenRegion.y, SourceSize.y)) * 2.0 / ScreenRegion.x, 1e-6);
	return 1.0 - smoothstep(1.0, 1.0 + feather, radius);
}
#endif
