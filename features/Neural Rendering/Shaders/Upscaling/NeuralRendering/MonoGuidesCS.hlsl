#include "Common/ReverseZ.hlsli"
cbuffer Dimensions : register(b0)
{
	uint2 Extent;
	uint2 Padding;
};
Texture2D<float> Depth : register(t0);
Texture2D<float2> Motion : register(t1);
RWTexture2D<float> CapturedDepth : register(u0);
RWTexture2D<float2> CapturedMotion : register(u1);
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= Extent))
		return;
	const float depth = FrameBuffer::ToStandardDepth(Depth.Load(int3(id.xy, 0)));
	const float2 motion = Motion.Load(int3(id.xy, 0));
	CapturedDepth[id.xy] = isfinite(depth) ? saturate(depth) : 1.0;
	CapturedMotion[id.xy] = all(isfinite(motion)) ? motion : 0.0;
}
