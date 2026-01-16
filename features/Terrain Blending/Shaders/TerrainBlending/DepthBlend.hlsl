// Compute blended depth (min of main and terrain) and write the R32 output used by terrain blending.

RWTexture2D<float> BlendedDepthTexture   : register(u0);

Texture2D<float> MainDepthTexture   : register(t0);
Texture2D<float> TerrainDepthTexture: register(t1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint w, h;
	BlendedDepthTexture.GetDimensions(w, h);
	if (DTid.x >= w || DTid.y >= h)
		return;

	float dMain = MainDepthTexture.Load(int3(DTid.xy, 0));
	float dTer  = TerrainDepthTexture.Load(int3(DTid.xy, 0));

	float mixedDepth = min(dMain, dTer);

	mixedDepth = saturate(mixedDepth);

	BlendedDepthTexture[DTid.xy] = mixedDepth;
}
