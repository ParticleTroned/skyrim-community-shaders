struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState SourceSampler : register(s0);
Texture2D<float4> SourceTex : register(t0);

cbuffer VRMenuComposite : register(b0)
{
	float2 SourceScale;
	float2 SourceOffset;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT output;
	output.Color = SourceTex.SampleLevel(SourceSampler, input.TexCoord * SourceScale + SourceOffset, 0);
	return output;
}
#endif
