#define main ProductionGrassMain
#include "RunGrass.hlsl"
#undef main

cbuffer Samples : register(b0)
{
	float CoatingCoverage;
	float ProjectedCoverage;
	uint PlantSurface;
};

struct ColorOutput
{
	float4 actual: SV_Target0;
	float4 expected: SV_Target1;
};

ColorOutput FoliageMaterial(float4 position : SV_Position)
{
	float3 leafColor = float3(0.08, 0.3, 0.04);
	MaterialProperties material = (MaterialProperties)0;
	material.BaseColor = leafColor;
	material.Thickness = lerp(0.2, 1, ProjectedCoverage);
	material.SubsurfaceColor = float3(0.2, 0.5, 0.1);
	material.FuzzWeight = 0.6 * (1 - ProjectedCoverage);
	material.CoatStrength = 0.7 * (1 - ProjectedCoverage);
	material.AO = 1;
	material.Roughness = 0.6;
	material.F0 = 0.04;
	SnowCover::FoliageSnowResult snow = SnowCover::ApplyGrassSnow(material.BaseColor, float3(0, 0, 1), float3(0, 0, 100 + CoatingCoverage),
		float2(0.5, 0.5), 0, 1, false, 0, TexBaseSampler, true);
	Foliage::ApplySnowCoating(material, snow.Coverage, snow.SubstrateColor);
	bool snowActive = PlantSurface && SharedData::snowCoverSettings.EnableSnowCover;
	if (snowActive && SharedData::snowCoverSettings.AffectGrassTint)
		SnowCover::ApplyFoliageColor(leafColor, CoatingCoverage);
	float coverage = snowActive ? saturate(CoatingCoverage * SharedData::snowCoverSettings.MainTint.a + SharedData::snowCoverSettings.TimeSnowing) : 0;
	coverage *= SharedData::snowCoverSettings.EnableExpensiveFoliage ? 1 : 0.55;
	coverage *= 1 - ProjectedCoverage;
	if (coverage < 0.01)
		coverage = 0;
	const float thickness = lerp(lerp(0.2, 1, ProjectedCoverage), 1, coverage);
	const float fuzz = 0.6 * (1 - ProjectedCoverage) * (1 - coverage);
	const float coat = 0.7 * (1 - ProjectedCoverage) * (1 - coverage);
	MaterialProperties reference = material;
	reference.Thickness = thickness;
	reference.SubsurfaceColor = float3(0.2, 0.5, 0.1);
	reference.FuzzWeight = fuzz;
	reference.CoatStrength = coat;
	reference.FoliageSnowCoverage = 0;
	reference.BaseColor = leafColor * (1 - coverage);
	ColorOutput result;
	if (position.x < 1) {
		result.actual = float4(material.Thickness, material.FuzzWeight, material.CoatStrength, snow.Coverage);
		result.expected = float4(thickness, fuzz, coat, coverage);
	} else if (position.x < 2) {
		DirectContext context = CreateDirectLightingContext(float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1),
			float3(0, 0, 1), float3(0, 0, 1), normalize(float3(1, 0, -1)), normalize(float3(1, 0, -1)), 1, 1, 1);
		DirectLightingOutput actualLight, expectedLight;
		GrassPBR::GetDirectLight(actualLight, context, material, false);
		GrassPBR::GetDirectLight(expectedLight, context, reference, false);
		result.actual = float4(actualLight.transmission, 1);
		result.expected = float4(expectedLight.transmission, 1);
	} else {
		IndirectContext context = CreateIndirectLightingContext(float3(0, 0, 1), float3(0, 0, 1), float3(0.6, 0, 0.8));
		IndirectLobeWeights actualLight, expectedLight;
		reference.BaseColor = material.BaseColor;
		GrassPBR::GetIndirectLobeWeights(actualLight, context, material, true);
		GrassPBR::GetIndirectLobeWeights(expectedLight, context, reference, true);
		result.actual = float4(actualLight.diffuse, 1);
		result.expected = float4(expectedLight.diffuse, 1);
	}
	return result;
}
