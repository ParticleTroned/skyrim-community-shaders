// Snapshot category provenance and raw depth at the same command-stream point.
// Shader loads support cropped depth capture without a partial DSV-resource copy.
cbuffer CaptureCB : register(b0)
{
	uint4 CaptureRegion; // packed-stereo offset.xy, extent.zw
};
Texture2D<unorm float2> SourceCategories : register(t0);
Texture2D<float> SourceDepth : register(t1);
RWTexture2D<unorm float2> FrozenCategories : register(u0);
RWTexture2D<float> FrozenDepth : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= CaptureRegion.zw))
		return;
	const uint2 pixel = CaptureRegion.xy + id.xy;
	FrozenCategories[pixel] = SourceCategories.Load(int3(pixel, 0));
	FrozenDepth[pixel] = SourceDepth.Load(int3(pixel, 0));
}
