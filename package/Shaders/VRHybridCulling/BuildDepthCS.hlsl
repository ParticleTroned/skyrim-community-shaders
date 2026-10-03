#include "Common/DepthOrder.hlsli"

Texture2D<float> SourceDepth : register(t0);
RWTexture2DArray<float> OutputDepth : register(u0);

cbuffer BuildConstants : register(b0)
{
	uint4 EyeRect[2];
	uint2 OutputSize;
	uint SourceReduction;
	uint Reserved;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint outputWidth, outputHeight, outputLayers;
	OutputDepth.GetDimensions(outputWidth, outputHeight, outputLayers);
	if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight || dispatchID.z >= outputLayers)
		return;
	if (dispatchID.z >= 2 || outputLayers != 2 || any(OutputSize != uint2(outputWidth, outputHeight)) ||
		SourceReduction < 1 || SourceReduction > 8 || (SourceReduction & (SourceReduction - 1)) != 0) {
		OutputDepth[dispatchID] = DepthOrder::Far();
		return;
	}

	uint sourceWidth, sourceHeight;
	SourceDepth.GetDimensions(sourceWidth, sourceHeight);
	uint4 rect = EyeRect[dispatchID.z];
	if (any(rect.zw == 0) || any(rect.zw > uint2(sourceWidth, sourceHeight)) ||
		any(rect.xy > uint2(sourceWidth, sourceHeight) - rect.zw)) {
		OutputDepth[dispatchID] = DepthOrder::Far();
		return;
	}

	float farthestDepth = DepthOrder::Near();
	uint2 firstPixel = dispatchID.xy * SourceReduction;
	if (any(firstPixel >= rect.zw)) {
		OutputDepth[dispatchID] = DepthOrder::Far();
		return;
	}
	[loop] for (uint y = 0; y < SourceReduction; ++y)
	{
		[loop] for (uint x = 0; x < SourceReduction; ++x)
		{
			uint2 pixel = firstPixel + uint2(x, y);
			float depth = DepthOrder::Far();
			if (all(pixel < rect.zw)) {
				float sampleDepth = SourceDepth.Load(int3(rect.xy + pixel, 0));
				// Zero is also the native VR mask; it cannot prove scene occlusion.
				if (isfinite(sampleDepth) && sampleDepth > 0.0 && sampleDepth <= 1.0)
					depth = sampleDepth;
			}
			farthestDepth = DepthOrder::Farthest(farthestDepth, depth);
		}
	}
	OutputDepth[dispatchID] = farthestDepth;
}
