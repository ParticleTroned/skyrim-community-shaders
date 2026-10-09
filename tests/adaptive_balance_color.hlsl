#if defined(FIRE_TEST) || defined(APPEARANCE_TEST)
#	define LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS
#endif
#include "Common/AdaptiveBalanceColor.hlsli"
#if defined(FIRE_TEST)
#	include "Common/AdaptiveBalanceFire.hlsli"
#endif

cbuffer Samples : register(b0)
{
	float4 colors[8];
#if defined(POINT_LIGHT_SATURATION_TEST)
	uint4 lightParameters;
#elif defined(APPEARANCE_TEST)
	uint4 appearanceParameters;
	float4 appearanceControls;
	float4 appearanceTint;
	float4 appearanceMaterial;
#endif
};
RWTexture2D<float4> Result : register(u0);

[numthreads(8, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
#if defined(FIRE_TEST) || defined(POINT_LIGHT_SATURATION_TEST) || defined(APPEARANCE_TEST)
	// FXC can sink a dynamic sample index into only one grading branch.
	float3 color = colors[0].rgb;
	[unroll] for (uint sample = 1; sample < 8; ++sample)
		color = id.x == sample ? colors[sample].rgb : color;
#else
	float3 color = colors[id.x].rgb;
#endif
#if defined(APPEARANCE_TEST)
	float3 graded = color;
	float3 original = color;
	if (appearanceParameters.x == 0) {
		graded = AdaptiveBalanceAppearance::ApplyColor(color, appearanceControls.x, appearanceControls.y, appearanceControls.z, appearanceTint.xyz);
	} else if (appearanceParameters.x == 1) {
		bool isLinear = appearanceParameters.y != 0;
		graded = Color::DirectionalLight(color, isLinear) * appearanceControls.w;
		original = Color::Light(color, isLinear) * ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
		           SharedData::adaptiveBalanceSettings.directionalLightMult * appearanceControls.w;
	} else if (appearanceParameters.x == 2) {
		graded = Color::ApplyAmbientBalance(color) * appearanceMaterial.xyz;
		original = color * Color::AmbientBalanceMultiplier() * appearanceMaterial.xyz;
	} else if (appearanceParameters.x == 3) {
		graded = Color::ApplyAmbientBalanceLinear(color) * appearanceMaterial.xyz;
		original = color * Color::IrradianceToLinear(Color::AmbientBalanceMultiplier()) * appearanceMaterial.xyz;
	} else if (appearanceParameters.x == 5) {
		bool isLinear = appearanceParameters.y != 0;
		graded = Color::DirectionalLight(color, isLinear, appearanceControls.w);
		original = Color::Light(color * appearanceControls.w, isLinear) *
		           ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) * SharedData::adaptiveBalanceSettings.directionalLightMult;
	} else if (appearanceParameters.x == 6) {
		bool isLinear = appearanceParameters.y != 0;
		graded = Color::PointLight(color, isLinear);
		original = Color::Light(color, isLinear) * ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
		           Color::GetPointLightMultiplier(isLinear);
	} else {
		graded = lerp(appearanceMaterial.xyz, AdaptiveBalanceAppearance::ApplyFog(color), appearanceControls.w);
		original = lerp(appearanceMaterial.xyz, color, appearanceControls.w);
	}
	Result[id.xy] = float4(graded, all(asuint(graded) == asuint(original)));
#elif defined(FIRE_TEST)
	float3 graded = AdaptiveBalanceFire::Apply(color);
	float diagnostic = SharedData::adaptiveBalanceSettings.fireIntensity +
	                   8.0 * AdaptiveBalanceFire::IsFire() + 16.0 * Color::IsSceneColorDraw();
	Result[id.xy] = float4(graded, diagnostic);
#elif defined(POINT_LIGHT_SATURATION_TEST)
	bool isLinear = lightParameters.x != 0;
	uint lightFlags = lightParameters.y == 1 ? Color::PointLightFlagSpot :
	                  lightParameters.y == 2 ? Color::PointLightFlagOmnidirectionalBulb :
	                                           0;
	bool preserveHDRIntensity = lightParameters.z != 0;
	float3 graded = lightParameters.w != 0 ? Color::DirectionalLight(color, isLinear) :
	                preserveHDRIntensity   ? Color::PointLightPreserveHDRIntensity(color, isLinear, lightFlags) :
	                                         Color::PointLight(color, isLinear, lightFlags);
	float3 original = Color::Light(color, isLinear, preserveHDRIntensity) *
	                  ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
	                  Color::GetPointLightMultiplier(isLinear) * Color::GetPointLightTypeMultiplier(isLinear, lightFlags);
	Result[id.xy] = float4(graded, all(asuint(graded) == asuint(original)));
#else
	if (!ENABLE_LL)
		color = Color::LinearToGammaSafe(color);
	float3 graded = AdaptiveBalanceColor::Apply(color,
		SharedData::adaptiveBalanceSettings.contrast, SharedData::adaptiveBalanceSettings.saturation, ENABLE_LL);
	if (!ENABLE_LL)
		graded = Color::GammaToLinearSafe(graded);
	Result[id.xy] = float4(graded, all(AdaptiveBalanceColor::Apply(color, 1.0, 1.0, ENABLE_LL) == color));
#endif
}
