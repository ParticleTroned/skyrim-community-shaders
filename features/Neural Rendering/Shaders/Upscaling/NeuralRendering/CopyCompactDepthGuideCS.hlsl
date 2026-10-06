Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> OutputDepth : register(u0);

cbuffer CopyDepthGuideCB : register(b0)
{
	uint2 SourceOrigin;
	uint2 Extent;
};

[numthreads(8, 8, 1)] void main(uint3 thread : SV_DispatchThreadID) {
	if (any(thread.xy >= Extent))
		return;
	OutputDepth[thread.xy] = SourceDepth.Load(uint3(thread.xy + SourceOrigin, 0));
}
