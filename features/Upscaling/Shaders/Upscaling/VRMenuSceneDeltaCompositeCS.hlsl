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
	const float neutralPanelMaxChroma = 0.18;
	const float neutralPanelChromaGain = 8.0;
	const float neutralPanelChromaReductionGain = 4.0;
	const float neutralPanelMinLuma = 0.35;
	const float neutralPanelLumaGain = 6.0;
	const float assumedPanelMinAlpha = 0.60;

	float deltaMagnitude = max(max(abs(delta.r), abs(delta.g)), abs(delta.b));
	if (deltaMagnitude <= menuDeltaThreshold)
		return;

	// Start from the proven text-friendly delta path, then suppress only part of
	// the reconstructed scene residual under broad neutral menu panels. A plain
	// baked replacement is still semi-transparent because the vanilla bake is
	// already blended with worldspace, so panel pixels need a bounded opacity
	// recovery while dark glyphs and colored borders stay on the original
	// sharp-text path.
	float menuCoverage = saturate((deltaMagnitude - menuDeltaThreshold) * menuCoverageGain);
	float4 current = Output[dispatchThreadID.xy];

	float bakedMax = max(max(baked.r, baked.g), baked.b);
	float bakedMin = min(min(baked.r, baked.g), baked.b);
	float bakedChroma = bakedMax - bakedMin;
	float cleanMax = max(max(clean.r, clean.g), clean.b);
	float cleanMin = min(min(clean.r, clean.g), clean.b);
	float cleanChroma = cleanMax - cleanMin;
	float bakedLuma = dot(baked.rgb, float3(0.2126, 0.7152, 0.0722));
	float lowBakedChroma = saturate((neutralPanelMaxChroma - bakedChroma) * neutralPanelChromaGain);
	float chromaPulledNeutral = saturate((cleanChroma - bakedChroma) * neutralPanelChromaReductionGain);
	float neutralPanel =
		max(lowBakedChroma, chromaPulledNeutral) *
		saturate((bakedLuma - neutralPanelMinLuma) * neutralPanelLumaGain);
	float panelCoverage = menuCoverage * neutralPanel;
	float3 baseline = current.rgb + delta;
	float3 opaquePanelEstimate = clean.rgb + (delta * rcp(assumedPanelMinAlpha));

	Output[dispatchThreadID.xy] = float4(lerp(baseline, opaquePanelEstimate, panelCoverage), current.a);
}
