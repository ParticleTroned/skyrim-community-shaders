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
	float CoatingCoverage;
	float ProjectedCoverage;
	uint PlantSurface;
	uint ProjectedTexel;
	float3 SelectionNormals;
	float3 SelectionPosition;
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
		SnowCover::CoverageInputs footprint = SnowCover::GetSurfaceFootprint(normal, 0, 0, 1, 100, 0, 0, 1);
		SnowCover::FireMeltSample fireMelt = (SnowCover::FireMeltSample)0;
#if defined(TRUE_PBR)
		SnowCover::ApplySnowPBR(material, normal, 0, footprint, 0, 0, fireMelt);
		actual = material.BaseColor * Color::PBRLightingScale;
#else
		SnowCover::ApplySnow(material, normal, 0, footprint, 0, 0, fireMelt);
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

#if defined(PROJECTED_UV)
float4 ProjectedColor(float4 position : SV_Position) : SV_Target0
{
#	if defined(TRUE_PBR)
	const bool pbr = true;
#	else
	const bool pbr = false;
#	endif
	float3 color;
	if (position.x < 1) {
		color = Color::ProjectedTextureColor(TexProjDiffuseSampler.Load(int3(ProjectedTexel, 0, 0)).rgb);
		if (!pbr)
			color *= Color::VanillaDiffuseColorMult();
	} else if (position.x < 2) {
		color = SnowCover::SnowDiffuse(SnowCover::SnowAlbedo.Load(int3(ProjectedTexel, 0, 0)).rgb, false, pbr);
	} else {
		color = Color::Diffuse(SnowCover::SnowAlbedo.Load(int3(ProjectedTexel, 0, 0)).rgb, pbr);
	}
	return float4(color, ENABLE_LL_COLOR_ADJUSTMENTS ? 2 : 1);
}
#endif

float4 WeatherRange(float4 position : SV_Position) : SV_Target0
{
	float3 p = float3(position.x * 100, 0, position.x * 50);
	float range = SnowCover::GetWeatherRange(p, 1000);
	float fadeStart = SharedData::snowCoverSettings.WeatherFadeStart;
	float fadeEnd = max(fadeStart + 1, SharedData::snowCoverSettings.WeatherFadeEnd);
	float wobble = 1000 * sin(p.z * 0.001 + cos(p.x * p.y * 0.001));
	float expected = 1 - smoothstep(fadeStart, max(fadeStart + 1, fadeEnd + wobble), 1000);
#if !(CSX_PERF_REFERENCE_FLAGS & CSX_PERF_SNOW_WEATHER_RANGE)
	if (SharedData::snowCoverSettings.TimeSnowing == 0)
		expected = 0;
#endif
	return float4(range, expected, 0, 1);
}

ColorOutput MaterialSelection(float4 position : SV_Position)
{
	float3 normal = float3(0, 0, SelectionNormals[(uint)position.x]);
	SnowCover::CoverageInputs footprint = (SnowCover::CoverageInputs)0;
	footprint.position = SelectionPosition + float3(position.x * 997, 0, 0);
	footprint.scale = footprint.environment = footprint.weatherRange = 1;
	footprint.weatherUpness = max(SharedData::snowCoverSettings.minAngle, normal.z);
	SnowCover::FireMeltSample fire = (SnowCover::FireMeltSample)0;
	float2 uv = 0;
	bool alternate;
	float coverage = SnowCover::ApplySnowBase(normal, uv, alternate, 0, footprint, 0, fire);
	float accumulation = SharedData::snowCoverSettings.TimeSnowing;
	float weather = accumulation * accumulation * accumulation * max(500, SharedData::snowCoverSettings.SnowingDensity) / 500;
	weather = clamp(weather * max(SharedData::snowCoverSettings.minAngle, normal.z), -1, 1);
	float mainScore = (1 - abs(normal.z - SharedData::snowCoverSettings.peakMainAngle)) + min(0, weather) * SharedData::snowCoverSettings.minAngle;
	float3 p = footprint.position;
	float alternateScore = (1 - abs(normal.z - SharedData::snowCoverSettings.peakAltAngle)) + sin(p.z * 0.01 + cos(p.x * p.y * 0.01) * 0.025) * 0.05;
	ColorOutput result;
	result.actual = float4(alternate, coverage, 0, 1);
	result.expected = float4(alternateScore > mainScore, 1, 0, 1);
	return result;
}

float4 ObjectFadeCoverage(float4 position : SV_Position) : SV_Target0
{
	float viewDist = floor(position.x) * 200;
	SnowCover::CoverageInputs footprint = SnowCover::GetSurfaceFootprint(float3(0, 0, 1), 0, 0, 1, 100, viewDist, 0, 1);
	SnowCover::FireMeltSample fireMelt = (SnowCover::FireMeltSample)0;
	MaterialProperties material = (MaterialProperties)0;
	float3 normal = float3(0, 0, 1);
#if defined(TRUE_PBR)
	float coverage = SnowCover::ApplySnowPBR(material, normal, 0, footprint, viewDist, 0, fireMelt);
#else
	float coverage = SnowCover::ApplySnow(material, normal, 0, footprint, viewDist, 0, fireMelt);
#endif
	return float4(footprint.scale, SnowCover::GetCoverage(footprint, 0, 0), coverage, 1);
}

#if defined(TRUE_PBR)
ColorOutput FoliageMaterial(float4 position : SV_Position)
{
	float3 leafColor = float3(0.08, 0.3, 0.04);
	MaterialProperties material = (MaterialProperties)0;
	material.BaseColor = leafColor;
	material.Thickness = lerp(0.2, 1, ProjectedCoverage);
	material.SubsurfaceColor = float3(0.2, 0.5, 0.1);
	material.FuzzWeight = 0.6 * (1 - ProjectedCoverage);
	material.FuzzColor = float3(0.1, 0.2, 0.05);
	material.CoatStrength = 0.7 * (1 - ProjectedCoverage);
	material.CoatRoughness = 0.5;
	material.CoatF0 = 0.04;
	material.AO = 1;
	material.Roughness = 0.6;
	material.F0 = 0.04;
	material.GlintLogMicrofacetDensity = SnowCover::DisabledGlintDensity;
	float3 normal = float3(0, 0, 1);
	SnowCover::CoverageInputs footprint = SnowCover::GetSurfaceFootprint(normal, 0, 0, 1, 100, 0, 0, CoatingCoverage);
	SnowCover::FireMeltSample fireMelt = (SnowCover::FireMeltSample)0;
	SnowCover::ApplySnowPBR(material, normal, 0, footprint, 0, 0, fireMelt);
#	if defined(WETTERNESS)
	float porosity = 1;
	ApplyWetnessDarkening(material, porosity, float3(0, 0, 100), 0, true, 1, 0.5, 1, 1);
	const float substratePorosity = (PBRFlags & PBR::Flags::TwoLayer) ? 0 : 1;
	leafColor = lerp(leafColor, pow(abs(leafColor), 1 + 0.5 * substratePorosity), 0.8);
	leafColor = lerp(leafColor, pow(abs(leafColor), 1 + substratePorosity), 0.5);
#	endif
#	if defined(TREE_ANIM)
	const bool foliage = true;
#	else
	const bool foliage = PlantSurface != 0;
#	endif
	const float coverage = foliage ? CoatingCoverage : 0;
	const float thickness = lerp(lerp(0.2, 1, ProjectedCoverage), 1, coverage);
	const float fuzz = 0.6 * (1 - ProjectedCoverage) * (1 - coverage);
	const float coat = 0.7 * (1 - ProjectedCoverage) * (1 - coverage);
	MaterialProperties reference = material;
	reference.Thickness = thickness;
	reference.SubsurfaceColor = float3(0.2, 0.5, 0.1);
	reference.FuzzWeight = fuzz;
	reference.CoatStrength = coat;
#	if defined(TREE_ANIM)
	reference.FoliageSnowCoverage = 0;
#	endif
	reference.BaseColor = foliage ? leafColor * (1 - coverage) : material.BaseColor;
	ColorOutput result;
	if (position.x < 1) {
		result.actual = float4(material.Thickness, material.FuzzWeight, material.CoatStrength, 1);
		result.expected = float4(thickness, fuzz, coat, 1);
	} else if (position.x < 2) {
		DirectContext context = CreateDirectLightingContext(float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1),
			float3(0, 0, 1), float3(0, 0, 1), normalize(float3(1, 0, -1)), normalize(float3(1, 0, -1)), 1, 1, 1);
		DirectLightingOutput actualLight, expectedLight;
		const float3x3 identity = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
		PBR::GetDirectLightInput(actualLight, context, material, identity, 0);
		PBR::GetDirectLightInput(expectedLight, context, reference, identity, 0);
		result.actual = float4(actualLight.transmission, 1);
		result.expected = float4(expectedLight.transmission, 1);
	} else {
		IndirectContext context = CreateIndirectLightingContext(float3(0, 0, 1), float3(0, 0, 1), float3(0.6, 0, 0.8));
		IndirectLobeWeights actualLight, expectedLight;
		reference.BaseColor = material.BaseColor;
#	if defined(TREE_ANIM)
		reference.FoliageSnowCoverage = 1;
#	endif
		PBR::GetIndirectLobeWeights(actualLight, context, material);
		PBR::GetIndirectLobeWeights(expectedLight, context, reference);
#	if defined(TREE_ANIM)
		if (SharedData::foliageLightingSettings.EnableFoliageAmbientBoost)
			expectedLight.diffuse += leafColor * (1 - coverage) * SharedData::foliageLightingSettings.FoliageAmbientAmount;
#	endif
		result.actual = float4(actualLight.diffuse, 1);
		result.expected = float4(expectedLight.diffuse, 1);
	}
	return result;
}
#endif
