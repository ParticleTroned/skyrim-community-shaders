Texture2D<float4> CleanScene : register(t0);
Texture2D<float4> BakedScene : register(t1);
RWTexture2D<float4> Output : register(u0);
SamplerState LinearSampler : register(s0);

cbuffer VRMenuSceneDeltaCompositeCB : register(b0)
{
	float2 OutputSize;
	float2 SourceTextureSize;
	float2 SourceUVScale;
	float2 SourceUVOffset;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= (uint)OutputSize.x || dispatchThreadID.y >= (uint)OutputSize.y)
		return;

	float2 outputUV = (float2(dispatchThreadID.xy) + float2(0.5, 0.5)) / max(OutputSize, float2(1.0, 1.0));
	float2 sourceTexel = 1.0 / max(SourceTextureSize, float2(1.0, 1.0));
	float2 sourceUV = outputUV * SourceUVScale + SourceUVOffset;
	sourceUV = clamp(sourceUV, sourceTexel * 0.5, 1.0 - sourceTexel * 0.5);

	float4 clean = CleanScene.SampleLevel(LinearSampler, sourceUV, 0.0);
	float4 baked = BakedScene.SampleLevel(LinearSampler, sourceUV, 0.0);
	float4 delta = baked - clean;

	float deltaMagnitude = max(max(abs(delta.r), abs(delta.g)), max(abs(delta.b), abs(delta.a)));
	if (deltaMagnitude <= (1.0 / 1024.0))
		return;

	float4 current = Output[dispatchThreadID.xy];
	current += delta;
	Output[dispatchThreadID.xy] = current;
}
