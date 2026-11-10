#include "SubsurfaceScattering/SkinColor.hlsli"

cbuffer Samples : register(b0)
{
	float4 originals[8];
	float4 scattered[8];
	float4 controls;
	float mask;
};

RWTexture2D<float4> Result : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	// Keep FXC from sinking dynamic constant indexing into the neutral branch.
	float4 original = originals[0];
	float3 color = scattered[0].rgb;
	[unroll] for (uint sample = 1; sample < 8; ++sample)
	{
		original = id.x == sample ? originals[sample] : original;
		color = id.x == sample ? scattered[sample].rgb : color;
	}
	Result[id.xy] = float4(BurleySkinColor(original.rgb, color,
							   controls.x, controls.y, controls.z, controls.w, mask),
		original.a);
}
