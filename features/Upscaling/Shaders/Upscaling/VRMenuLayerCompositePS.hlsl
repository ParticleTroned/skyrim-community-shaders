#include "Upscaling/UpscaleVS.hlsl"

Texture2D sourceTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer VRMenuLayerCompositeCB : register(b0)
{
	float2 sourceScale;
	float2 sourceOffset;
};

float4 main(VS_OUTPUT input) : SV_Target
{
	float2 sourceUV = saturate(input.TexCoord) * sourceScale + sourceOffset;
	return sourceTexture.SampleLevel(linearSampler, sourceUV, 0.0);
}
