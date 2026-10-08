#include "Upscaling/UpscaleVS.hlsl"

Texture2D sourceTexture : register(t0);
#ifdef POINTER_OVERLAY
Texture2D pointerTexture : register(t1);
#endif
SamplerState linearSampler : register(s0);

cbuffer VRMenuLayerCompositeCB : register(b0)
{
	float2 sourceScale;
	float2 sourceOffset;
};

float4 main(VS_OUTPUT input) : SV_Target
{
	float2 sourceUV = saturate(input.TexCoord) * sourceScale + sourceOffset;
	float4 menu = sourceTexture.SampleLevel(linearSampler, sourceUV, 0.0);
#ifdef POINTER_OVERLAY
	uint width, height;
	pointerTexture.GetDimensions(width, height);
	float2 halfTexel = 0.5 / float2(width, height);
	sourceUV = clamp(sourceUV, sourceOffset + halfTexel, sourceOffset + sourceScale - halfTexel);
	float4 pointer = pointerTexture.SampleLevel(linearSampler, sourceUV, 0.0);
	return pointer + menu * (1.0 - pointer.a);
#else
	return menu;
#endif
}
