Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> OutputDepth : register(u0);

cbuffer CopyDepthGuideCB : register(b0)
{
	uint2 RoiOffset;
	uint2 RoiExtent;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
	if (any(dispatchThreadId.xy >= RoiExtent))
		return;
	uint2 pixel = dispatchThreadId.xy + RoiOffset;
	uint width = 0;
	uint height = 0;
	OutputDepth.GetDimensions(width, height);
	if (pixel.x >= width || pixel.y >= height)
		return;

	OutputDepth[pixel] = SourceDepth.Load(uint3(pixel, 0));
}
