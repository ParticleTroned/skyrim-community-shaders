#define LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS
#include "../../tests/point_light_colour_reference.hlsli"
#include "Common/Color.hlsli"
namespace LightLimitFix
{
#include "LightLimitFix/Common.hlsli"
#include "LightLimitFix/PreparedPointLightColour.hlsli"
}
StructuredBuffer<LightLimitFix::Light> lights : register(t35);
cbuffer Samples : register(b0)
{
	uint strictLight;
	uint preserveHDR;
	float attenuation;
	uint pad;
};

float4 main(float4 position : SV_Position) : SV_Target
{
	uint index = (uint)position.x;
	LightLimitFix::Light light = lights[index];
	const bool authoredLinear = (light.lightFlags & LightLimitFix::LightFlags::Linear) != 0;
	const bool reflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0;
	float3 colour;
#if defined(POINT_LIGHT_REFERENCE_TEST)
	colour = PointLightReference::PointLight(light.color, authoredLinear, light.lightFlags, preserveHDR != 0);
#else
	if (preserveHDR) {
		colour = Color::PointLightPreserveHDRIntensity(light.color, authoredLinear, light.lightFlags);
	} else {
		colour = LightLimitFix::GetPointLightColour(light, index, !strictLight && LightLimitFix::UsePreparedPointLightColour(reflection));
	}
#endif
	return float4(colour * attenuation * light.fade, 0.0);
}
