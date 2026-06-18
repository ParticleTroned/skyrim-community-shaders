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
	float3 delta = baked.rgb - clean.rgb;

	const float menuDeltaThreshold = 1.0 / 1024.0;
	const float menuCoverageGain = 16.0;
	const float maxSceneSuppression = 0.35;

	float deltaMagnitude = max(max(abs(delta.r), abs(delta.g)), abs(delta.b));
	if (deltaMagnitude <= menuDeltaThreshold)
		return;

	// Keep the menu contribution out of vendor temporal reconstruction by
	// applying only the real baked-vs-clean delta. A full baked replacement
	// reintroduces the raw render-scale scene/menu source and makes text flicker
	// again. Derive coverage from RGB only, then damp the reconstructed scene
	// under visible menu coverage so translucent menu backgrounds leak less
	// world detail without alpha noise creating halos or rerouting the frame.
	float menuCoverage = saturate((deltaMagnitude - menuDeltaThreshold) * menuCoverageGain);
	float sceneKeep = 1.0 - (menuCoverage * maxSceneSuppression);
	float4 current = Output[dispatchThreadID.xy];
	Output[dispatchThreadID.xy] = float4((current.rgb * sceneKeep) + delta, current.a);
}
