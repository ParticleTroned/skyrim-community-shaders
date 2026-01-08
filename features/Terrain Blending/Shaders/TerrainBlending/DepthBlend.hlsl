// DepthBlend.hlsl
//
// Computes a "closest-depth" field used by Terrain Blending:
//   blendedDepth = min(mainDepth, terrainDepth)
//
// The output is written twice:
//   u0: R32_FLOAT UAV (debug / high precision)
//   u1: R16_UNORM UAV (cheap sampling in Lighting; quantized but sufficient)
//
// NOTE:
//  - We intentionally use plain float types here (no 'unorm' modifiers) for maximum compiler compatibility.
//  - We also guard dispatch bounds, since Dispatch() uses ceil(width/8), ceil(height/8).

RWTexture2D<float> BlendedDepthTexture   : register(u0);
RWTexture2D<float> BlendedDepthTexture16 : register(u1);

Texture2D<float> MainDepthTexture   : register(t0);
Texture2D<float> TerrainDepthTexture: register(t1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint w, h;
	BlendedDepthTexture.GetDimensions(w, h);
	if (DTid.x >= w || DTid.y >= h)
		return;

	// Depth SRVs are bound with the correct view formats (e.g. R24_UNORM_X8 / R32_FLOAT),
	// so we can read as float.
	float dMain = MainDepthTexture.Load(int3(DTid.xy, 0));
	float dTer  = TerrainDepthTexture.Load(int3(DTid.xy, 0));

	// Choose the nearer surface (D3D depth: smaller is closer in the common case).
	float mixedDepth = min(dMain, dTer);

	// Helps keep the R16_UNORM UAV write well-defined even if inputs contain tiny overshoots.
	mixedDepth = saturate(mixedDepth);

	BlendedDepthTexture[DTid.xy]   = mixedDepth;
	BlendedDepthTexture16[DTid.xy] = mixedDepth;
}
