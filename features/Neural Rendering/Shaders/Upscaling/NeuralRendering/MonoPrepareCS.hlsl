#include "Upscaling/NeuralRendering/MonoCommon.hlsli"
Texture2D<float4> Source : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4> ModelColor : register(u0);
RWTexture2D<float> ModelDepth : register(u1);
RWTexture2D<float2> ModelMotion : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= ModelSize)) return;
	float2 coordinate = float2(id.xy), samplingSize = float2(ModelSize);
	uint2 sourceStart = 0u, sourceEnd = SourceSize;
	int tile = -1;
	if (Packing.x > 0u) {
		tile = PackedTile(id.xy);
		if (tile < 0) {
			ModelColor[id.xy] = 0.0; ModelDepth[id.xy] = 1.0; ModelMotion[id.xy] = 0.0;
			return;
		}
		coordinate = float2(id.xy - TilePacked[tile].xy + TileModel[tile].xy);
		samplingSize = Packing.yz;
		sourceStart = TileSource[tile].xy;
		sourceEnd = sourceStart + TileSource[tile].zw;
	}
	float2 sourceLower = coordinate / samplingSize, sourceUpper = (coordinate + 1.0) / samplingSize;
	if (PeripheralProtected < 1.0) PeripheralSourceBounds(sourceLower, sourceUpper, sourceLower, sourceUpper);
	const float2 lower = max(saturate(sourceLower) * SourceSize, float2(sourceStart));
	const float2 upper = min(saturate(sourceUpper) * SourceSize, float2(sourceEnd));
	float4 color = 0.0;
	float area = 0.0;
	// Area filtering keeps fractional inference scales independent of subpixel phase.
	[loop] for (uint y = uint(floor(lower.y)); y < uint(ceil(upper.y)); ++y) {
		[loop] for (uint x = uint(floor(lower.x)); x < uint(ceil(upper.x)); ++x) {
			const uint2 source = min(uint2(x, y), sourceEnd - 1u);
			const float2 overlap = max(0.0, min(upper, float2(source + 1u)) - max(lower, float2(source)));
			const float weight = overlap.x * overlap.y;
			if (weight <= 0.0) continue;
			const float4 sampledColor = Source.Load(int3(source, 0));
			float3 prepared;
			const bool valid = ForwardColor(sampledColor.rgb, prepared) && all(prepared >= 0.0) && all(prepared <= 65504.0);
			color += float4(valid ? prepared : 0.0, isfinite(sampledColor.a) ? sampledColor.a : 1.0) * weight;
			area += weight;
		}
	}
	ModelColor[id.xy] = area > 0.0 ? color / area : 0.0;
	const int2 guideFirst = int2(floor(lower * GuideSize / SourceSize + Sampling.xy));
	const int2 guideEnd = int2(ceil(upper * GuideSize / SourceSize + Sampling.xy));
	float closest = 1.0;
	float2 vector = 0.0;
	bool found = false;
	[loop] for (int y = guideFirst.y; y < guideEnd.y; ++y) {
		[loop] for (int x = guideFirst.x; x < guideEnd.x; ++x) {
			const int2 pixel = clamp(int2(x, y), 0, int2(GuideSize) - 1);
			const float candidate = Depth.Load(int3(pixel, 0));
			const float2 velocity = Motion.Load(int3(pixel, 0));
			if (isfinite(candidate) && candidate >= 0.0 && candidate <= 1.0 && all(isfinite(velocity)) && (!found || candidate < closest)) {
				closest = candidate; vector = velocity; found = true;
			}
		}
	}
	ModelDepth[id.xy] = closest;
	float2 atlasMotion = vector * samplingSize / ModelSize;
	if (PeripheralProtected < 1.0) {
		const float2 anchor = PeripheralInverse((float2(id.xy) + 0.5) / ModelSize);
		atlasMotion = PeripheralForward(anchor + vector) - PeripheralForward(anchor);
		if (!all(isfinite(atlasMotion))) atlasMotion = 2.0;
	}
	if (tile >= 0) {
		const float2 position = float2(id.xy) + 0.5;
		const float2 reach = abs(atlasMotion) * ModelSize + 1.0;
		// An atlas lookup may not read a neighboring actor's temporal history.
		if (any(position - reach < TilePacked[tile].xy) || any(position + reach >= TilePacked[tile].xy + TilePacked[tile].zw))
			atlasMotion = 2.0;
	}
	ModelMotion[id.xy] = atlasMotion;
}
