// Numeric raw-depth extraction. Depth/stencil resources cannot legally be
// cropped with CopySubresourceRegion; load their SRV and store an R32_FLOAT UAV.
cbuffer DepthRegionCB : register(b0)
{
	uint4 Offsets;  // source xy, destination xy
	uint4 Extent;   // width, height, reserved, reserved
};
Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> DestinationDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= Extent.xy))
		return;
	DestinationDepth[Offsets.zw + id.xy] = SourceDepth.Load(int3(Offsets.xy + id.xy, 0));
}
