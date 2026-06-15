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
Texture2D<float4> FinalSceneTex : register(t0);
Texture2D<float4> CleanSceneTex : register(t1);
Texture2D<float4> BakedSceneTex : register(t2);

cbuffer VRMenuComposite : register(b0)
{
	float2 SourceScale;
	float2 SourceOffset;
};

PS_OUTPUT main(PS_INPUT input)
{
	const float2 sourceTexCoord = input.TexCoord * SourceScale + SourceOffset;
	const float4 finalScene = FinalSceneTex.SampleLevel(SourceSampler, input.TexCoord, 0);
	const float4 cleanScene = CleanSceneTex.SampleLevel(SourceSampler, sourceTexCoord, 0);
	const float4 bakedScene = BakedSceneTex.SampleLevel(SourceSampler, sourceTexCoord, 0);
	const float4 menuDelta = bakedScene - cleanScene;
	const float menuMagnitude = max(max(abs(menuDelta.r), abs(menuDelta.g)), max(abs(menuDelta.b), abs(menuDelta.a)));
	const float menuCoverage = saturate((menuMagnitude - (1.0 / 1024.0)) * 2048.0);

	PS_OUTPUT output;
	output.Color = finalScene + (menuDelta * menuCoverage);
	return output;
}
#endif
