#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"

Texture2D<float4> InputColor : register(t0);
Texture2D<float> InputDepth : register(t1);
Texture2D<float2> InputMotion : register(t2);
Texture2D<float> InputMask : register(t3);
RWTexture2D<float4> ProxyColor : register(u0);
RWTexture2D<float> ProxyDepth : register(u1);
RWTexture2D<float2> ProxyMotion : register(u2);
RWTexture2D<float> ProxyMask : register(u3);

void PrepareColor(uint2 id)
{
	if (any(id >= ModelRegionSize))
		return;
	uint2 pixel = ModelRegionOffset + id;
	float2 lower = max(float2(pixel) * float2(SourceSize) / float2(ModelSize), float2(SourceRegionOffset));
	float2 upper = min(float2(pixel + 1u) * float2(SourceSize) / float2(ModelSize), float2(SourceRegionOffset + SourceRegionSize));
	uint2 first = uint2(floor(lower));
	uint2 end = uint2(ceil(upper));
	float4 color = 0.0;
	float area = 0.0, mask = 0.0;
	// An area filter avoids phase-dependent shimmer when the model grid changes size.
	[loop] for (uint y = first.y; y < end.y; ++y)
	{
		[loop] for (uint x = first.x; x < end.x; ++x)
		{
			uint2 source = min(uint2(x, y), SourceRegionOffset + SourceRegionSize - 1u);
			float2 overlap = max(0.0, min(upper, float2(source + 1u)) - max(lower, float2(source)));
			float weight = overlap.x * overlap.y;
			if (weight <= 0.0)
				continue;
			float4 value = InputColor.Load(int3(source, 0));
			color += (all(isfinite(value)) ? value : 0.0) * weight;
			area += weight;
			if (HasMask != 0u) {
				float selection = InputMask.Load(int3(source, 0));
				mask = max(mask, isfinite(selection) ? saturate(selection) : 0.0);
			}
		}
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
	float2 lower = max(float2(pixel) * float2(guideSize) / float2(modelGuideSize), float2(sourceOffset));
	float2 upper = min(float2(pixel + 1u) * float2(guideSize) / float2(modelGuideSize), float2(sourceEnd));
	uint2 first = min(uint2(floor(lower)), guideSize - 1u);
	uint2 end = min(uint2(ceil(upper)), guideSize);
	float nearest = 1.0;
	float2 motion = 0.0;
	bool foundDepth = false;
	// Guide and colour grids retain their own resolutions and matching depth/motion samples.
	[loop] for (uint y = first.y; y < end.y; ++y)
	{
		[loop] for (uint x = first.x; x < end.x; ++x)
		{
			float depth = InputDepth.Load(int3(x, y, 0));
			float2 velocity = InputMotion.Load(int3(x, y, 0));
			if (isfinite(depth) && depth >= 0.0 && depth <= 1.0 && all(isfinite(velocity)) && (!foundDepth || depth < nearest)) {
				nearest = depth;
				motion = velocity;
				foundDepth = true;
			}
		}
	}
	ProxyDepth[pixel] = nearest;
	ProxyMotion[pixel] = motion * MotionVectorScale;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	PrepareColor(id.xy);
	PrepareGuides(id.xy);
}
