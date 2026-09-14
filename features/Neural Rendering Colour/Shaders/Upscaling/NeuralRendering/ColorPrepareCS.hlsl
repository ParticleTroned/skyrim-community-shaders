#include "ColorCommon.hlsli"
Texture2D<float4> Baseline : register(t0);
RWTexture2D<float4> Prepared : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= RegionSize))
		return;
	float4 original = Baseline.Load(int3(id.xy, 0));
	float3 value;
	// Invalid proxy inputs are excluded again during reconstruction. Black is
	// used as the finite model input, not an undocumented fallback colour space.
	bool valid = ForwardColor(original.rgb, value);
	Prepared[RegionOffset + id.xy] = float4(valid ? value : 0.0, original.a);
}
