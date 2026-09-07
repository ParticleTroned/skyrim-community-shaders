#include "Upscaling/NeuralRendering/OutputResolve.hlsli"

Texture2D<float4> OriginalColor : register(t0);
Texture2D<float4> RawModelOutput : register(t1);
RWTexture2D<float4> ResolvedOutput : register(u0);

cbuffer OutputResolveCB : register(b0)
{
	uint2 RoiOffset;
	uint2 RoiExtent;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (any(dispatchThreadId.xy >= RoiExtent))
		return;

	const uint2 pixel = dispatchThreadId.xy + RoiOffset;
	uint width = 0;
	uint height = 0;
	ResolvedOutput.GetDimensions(width, height);
	if (pixel.x >= width || pixel.y >= height)
		return;

	const int3 position = int3(pixel, 0);
	ResolvedOutput[pixel] = ResolveNeuralOutput(
		OriginalColor.Load(position),
		RawModelOutput.Load(position));
}
