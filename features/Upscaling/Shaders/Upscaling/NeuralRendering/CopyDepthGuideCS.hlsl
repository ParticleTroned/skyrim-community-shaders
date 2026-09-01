Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> OutputDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
	uint width = 0;
	uint height = 0;
	OutputDepth.GetDimensions(width, height);
	if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
		return;

	OutputDepth[dispatchThreadId.xy] = SourceDepth.Load(uint3(dispatchThreadId.xy, 0));
}
