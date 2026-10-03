#include "Common/DepthOrder.hlsli"

Texture2DArray<float> SourceDepth : register(t0);
RWTexture2DArray<float> OutputDepth : register(u0);

cbuffer ReduceConstants : register(b0)
{
	uint2 OutputSize;
	uint2 Reserved;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint outputWidth, outputHeight, outputLayers;
	OutputDepth.GetDimensions(outputWidth, outputHeight, outputLayers);
	if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight || dispatchID.z >= outputLayers)
		return;
	uint sourceWidth, sourceHeight, sourceLayers, sourceLevels;
	SourceDepth.GetDimensions(0, sourceWidth, sourceHeight, sourceLayers, sourceLevels);
	if (outputLayers != 2 || sourceLayers != 2 || sourceLevels != 1 ||
		any(OutputSize != uint2(outputWidth, outputHeight)) ||
		any(OutputSize != max(uint2(sourceWidth, sourceHeight) / 2, 1))) {
		OutputDepth[dispatchID] = DepthOrder::Far();
		return;
	}

	float farthestDepth = DepthOrder::Near();
	[unroll] for (uint y = 0; y < 2; ++y)
	{
		[unroll] for (uint x = 0; x < 2; ++x)
		{
			uint2 pixel = min(dispatchID.xy * 2 + uint2(x, y), uint2(sourceWidth, sourceHeight) - 1);
			float depth = SourceDepth.Load(int4(pixel, dispatchID.z, 0));
			if (!isfinite(depth) || depth <= 0.0 || depth > 1.0)
				depth = DepthOrder::Far();
			farthestDepth = DepthOrder::Farthest(farthestDepth, depth);
		}
	}
	OutputDepth[dispatchID] = farthestDepth;
}
