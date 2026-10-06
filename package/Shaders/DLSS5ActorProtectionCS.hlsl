cbuffer ActorProtection : register(b0)
{
	uint4 EvaluationRect;
	uint4 SelectionRect;
};

Texture2D<unorm float> ActorSelection : register(t0);
RWTexture2D<unorm float> ProviderProtection : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= EvaluationRect.zw))
		return;
	const uint2 pixel = EvaluationRect.xy + id.xy;
	float strength = 0.0;
	// Context outside current selection may contain stale mask storage.
	[branch] if (all(pixel >= SelectionRect.xy) && all(pixel < SelectionRect.xy + SelectionRect.zw))
		strength = saturate(ActorSelection.Load(int3(pixel, 0)));
	ProviderProtection[pixel] = 1.0 - strength;
}
