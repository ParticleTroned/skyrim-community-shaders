#define main ProductionLightingMain
#include "Lighting.hlsl"
#undef main

cbuffer Samples : register(b0)
{
	float3 MainSample;
	float MainAO;
	float3 AlternateSample;
	float AlternateAO;
	uint AlternateSurface;
};

struct ColorOutput
{
	float4 actual: SV_Target0;
	float4 expected: SV_Target1;
};

ColorOutput main(float4 position : SV_Position)
{
#if defined(TRUE_PBR)
	const bool pbr = true;
#else
	const bool pbr = false;
#endif
	uint route = (uint)position.x;
	bool alternate = route == 0 && AlternateSurface;
	float3 sample = alternate ? AlternateSample : MainSample;
	float3 tint = alternate ? SharedData::snowCoverSettings.AltTint.rgb : SharedData::snowCoverSettings.MainTint.rgb;
	float3 actual = 0;
	if (route == 0) {
		MaterialProperties material = (MaterialProperties)0;
		float3 normal = float3(0, 0, 1);
#if defined(TRUE_PBR)
		SnowCover::ApplySnowPBR(material, normal, 0, 0, 1, 100, 0, 0, 0, 1);
		actual = material.BaseColor * Color::PBRLightingScale;
#else
		SnowCover::ApplySnow(material, normal, 0, 0, 1, 100, 0, 0, 0, 1);
		actual = material.BaseColor;
#endif
	} else {
		SnowCover::ApplySnowFoliageBase(actual, float3(0, 0, 1), 0, 0, 1, pbr, route != 2);
	}

	// Installed sources match their native material's final diffuse, including LL controls.
	uint sourceMask = SharedData::snowCoverSettings.LandscapeSnowColorMask;
	bool installed = (sourceMask & (alternate ? 2u : 1u)) != 0;
	float3 expected;
	if (installed) {
		bool sourcePbr = (sourceMask & (alternate ? 8u : 4u)) != 0;
		expected = Color::Diffuse(sample, sourcePbr) * (sourcePbr ? Color::PBRLightingScale : 1);
	} else {
		// Retain configured-material behavior when automatic color is unavailable.
		expected = pbr ? sample : Color::LinearToSrgb(sample);
		if (route != 2)
			expected = Color::Diffuse(expected, pbr);
		expected *= Color::PBRLightingScale;
	}
	expected *= tint;
#if !defined(TRUE_PBR)
	if (route == 0)
		expected *= alternate ? AlternateAO : MainAO;
#endif
	ColorOutput result;
	result.actual = float4(actual, ENABLE_LL_COLOR_ADJUSTMENTS ? 2 : 1);
	result.expected = float4(expected, 1);
	return result;
}

float4 WeatherRange(float4 position : SV_Position) : SV_Target0
{
	float range = SnowCover::GetWeatherRange(float3(position.x * 100, 0, position.x * 50), 1000);
	return float4(range, 0, 0, 1);
}
