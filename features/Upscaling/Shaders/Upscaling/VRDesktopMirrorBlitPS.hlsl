#include "Upscaling/UpscaleVS.hlsl"

Texture2D sourceTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(VS_OUTPUT input) : SV_Target
{
	return sourceTexture.SampleLevel(linearSampler, saturate(input.TexCoord), 0.0);
}
