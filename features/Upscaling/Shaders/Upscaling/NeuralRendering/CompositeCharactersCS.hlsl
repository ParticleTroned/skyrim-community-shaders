cbuffer CompositeData : register(b0)
{
	uint2 OutputSize;
	uint DebugView;
	uint Padding;
	uint4 MaskSupport;
};

Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Neural : register(t1);
Texture2D<float> CharacterMask : register(t2);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= OutputSize))
		return;
	float4 baseline = Baseline[pixel];
	bool inSupport = all(pixel >= MaskSupport.xy) && all(pixel < MaskSupport.zw);
	float weight = 0.0;
	[branch] if (inSupport)
		weight = saturate(CharacterMask[pixel]);
	float3 color = baseline.rgb;
	// Zero mask pixels retain the exact sharpened DLSS baseline, even for invalid NR values.
	[branch] if (weight > 0.0 || DebugView == 3)
	{
		float3 neural = Neural[pixel].rgb;
		if (all(isfinite(neural)))
			color = DebugView == 3 ? neural : lerp(color, neural, weight);
	}
	if (DebugView == 1)
		color = weight.xxx;
	if (DebugView == 2 && inSupport &&
		(any(pixel - MaskSupport.xy < 2) || any(MaskSupport.zw - pixel <= 2)))
		color = float3(0.0, 1.0, 0.0);
	Output[pixel] = float4(color, baseline.a);
}
