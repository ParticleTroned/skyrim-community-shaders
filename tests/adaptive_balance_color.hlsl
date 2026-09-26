#include "Common/AdaptiveBalanceColor.hlsli"

cbuffer Samples : register(b0)
{
	float4 colors[8];
};
RWTexture2D<float4> Result : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	float3 color = colors[id.x].rgb;
	if (!ENABLE_LL)
		color = Color::LinearToGammaSafe(color);
	float3 graded = AdaptiveBalanceColor::Apply(color,
		SharedData::adaptiveBalanceSettings.contrast, SharedData::adaptiveBalanceSettings.saturation, ENABLE_LL);
	if (!ENABLE_LL)
		graded = Color::GammaToLinearSafe(graded);
	Result[id.xy] = float4(graded, all(AdaptiveBalanceColor::Apply(color, 1.0, 1.0, ENABLE_LL) == color));
}
