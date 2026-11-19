#define main LightingMain
#include "Lighting.hlsl"
#undef main

cbuffer TestInputs : register(b13)
{
	float directionalLuminance;
	float lightScale;
	float surfaceWaterHeight;
};
RWStructuredBuffer<float4> Results : register(u0);

#if defined(SKYLIGHTING)
[numthreads(64, 1, 1)] void CheckSurfaceChannels(uint3 id : SV_DispatchThreadID) {
	const float3 colors[4] = { float3(0.6, 0.6, 0.6), float3(0.45, 0.15, 0.07), float3(0.1, 0.45, 0.08), float3(0.72, 0.62, 0.42) };
	const float roughnesses[4] = { 0.2, 0.55, 0.8, 0.95 };
	uint surface = id.x % 256;
	float3 baseColor = colors[surface % 4];
	float roughness = roughnesses[surface % 4];
	float3 normal = surface % 8 < 4 ? float3(0, 0, 1) : normalize(float3(1, 0, 1));
	float3 view = normalize(float3(1, 0, 1));
	PS_INPUT input = (PS_INPUT)0;
	input.WorldPosition = float4(float(surface % 32) * 7.3, float(surface / 32) * 11.7, 100, 1);
	input.ModelPosition = input.WorldPosition.xyz;
	// Partial shelter keeps weak-rain bead occlusion active below the wet-film rain threshold.
	sh2 sky = Skylighting::UNIT_SH * 0.6;
	uint eyeIndex = 0;
#	if defined(VR)
	eyeIndex = id.x / 256;
#	endif
	WetnessSurfaceState wet = CreateWetnessSurfaceState(input, normal, normal, float3(0, 0, 500), view,
		surfaceWaterHeight, true, eyeIndex,
#	if !defined(SKIN) && !defined(HAIR)
		baseColor, roughness,
#	endif
		1.0, true, sky);
	MaterialProperties material = (MaterialProperties)0;
	material.BaseColor = baseColor;
	float porosity = 0.7;
	ApplyWetnessDarkening(material, porosity, float3(0, 0, 500), 0, wet.enabled != 0,
		wet.shoreDarkeningMask, wet.surfaceDarkeningMask, wet.rainDarkeningAbsorption, wet.shoreDarkeningAbsorption);
	Results[id.x * 6] = float4(material.BaseColor, porosity);
	Results[id.x * 6 + 1] = float4(wet.lightingStrength, wet.roughnessSpecular, wet.glossinessSpecular, wet.rainFilmWeight);
	Results[id.x * 6 + 2] = float4(wet.normal, wet.enabled);
	Results[id.x * 6 + 3] = float4(wet.filmHighlightReduction, wet.highlightReflectanceScale, wet.directSpecularScale, wet.raindropRippleMask);
	Results[id.x * 6 + 4] = float4(wet.rainCubemapSuppression, wet.puddleSkyReflectionScale, wet.terrainIndirectNormalStability, wet.shoreDarkeningMask);
	Results[id.x * 6 + 5] = float4(wet.surfaceDarkeningMask, wet.postRainBlend, wet.postRainOverridePhase, wet.postRainPuddleReflectionOverrideScale);
}
#endif

[numthreads(64, 1, 1)] void CheckRainFilmReflectivity(uint3 id : SV_DispatchThreadID)
{
	float strength = float(id.x % 32) / 31.0;
	float roughness = lerp(0.18, 0.4, float(id.x / 32) / 31.0);
	float3 normal = float3(0, 0, 1);
	WetReflectionParams reflection = (WetReflectionParams)0;
	reflection.legacyWeight = 1;
	reflection.effectiveScale = 1;
	reflection.skinOverdriveScale = 1;
	WetnessDirectLightingParams direct = CreateWetnessDirectLightingParams(normal, normal, strength, reflection);
	IndirectLobeWeights weights = (IndirectLobeWeights)1;
	IndirectContext context = CreateIndirectLightingContext(normal, normal, normal);
	float3 reflected = GetWetnessIndirectLobeWeights(weights, normal, roughness, strength, context, reflection);
	float2 brdf = BRDF::EnvBRDF(roughness, 1.0);
	float expected = saturate((direct.wetnessF0 * brdf.x + brdf.y) * direct.wetnessFScale);
	Results[id.x * 3] = float4(reflected, expected);
	Results[id.x * 3 + 1] = float4(weights.diffuse, strength);
	Results[id.x * 3 + 2] = float4(weights.specular, roughness);
}

[numthreads(64, 1, 1)] void CheckRainFilmHighlights(uint3 id : SV_DispatchThreadID) {
	float viewZ = lerp(0.02, 1.0, float(id.x % 32) / 31.0);
	float3 view = float3(sqrt(1.0 - viewZ * viewZ), 0, viewZ);
	float3 normal = float3(0, 0, 1);
	float3 light = float3(-view.x, 0, view.z);
	float roughness = lerp(0.18, 0.4, float(id.x / 32) / 31.0);
	float3 color = float3(4, 1, 0.25);
#if defined(TRUE_PBR)
	DirectContext context = CreateDirectLightingContext(normal, normal, normal, view, view, light, light, color, 0.5, 0.5);
#else
	DirectContext context = CreateDirectLightingContext(normal, normal, view, light, color, 0.5, 0.5);
#endif
	WetnessDirectLightingParams params = (WetnessDirectLightingParams)0;
	params.wetnessF0 = 0.02;
	params.wetnessFScale = 0.7;
	params.lightColorScale = 1;
	params.NdotV = viewZ;
	DirectLightingOutput reference = (DirectLightingOutput)0;
	reference.diffuse = float3(0.4, 0.3, 0.2);
	DirectLightingOutput corrected = reference;
	DirectLightingOutput attenuated = reference;
	EvaluateWetnessLighting(normal, context, roughness, params, reference);
	params.highlightReduction = 1;
	EvaluateWetnessLighting(normal, context, roughness, params, corrected);
	context.lightColor *= 0.125;
#if defined(TRUE_PBR)
	context.coatLightColor *= 0.125;
#endif
	EvaluateWetnessLighting(normal, context, roughness, params, attenuated);
	Results[id.x * 3] = float4(corrected.specular, dot(abs(corrected.diffuse - reference.diffuse), 1.0.xxx));
	Results[id.x * 3 + 1] = float4(reference.specular, roughness);
	float peak = id.x == 0 ? 0.0 : exp2(lerp(-10.0, 10.0, float(id.x) / 1023.0));
	float3 reflectance = float3(1, 0.25, 0.0625) * peak;
	float3 compressed = Wetterness::CompressWetReflectance(reflectance, 1);
	Results[id.x * 3 + 2] = float4(dot(abs(attenuated.specular - corrected.specular * 0.125), 1.0.xxx), compressed.x, compressed.y, compressed.z);
}

[numthreads(64, 1, 1)] void CheckRainFilmSurface(uint3 id : SV_DispatchThreadID)
{
	uint surface = id.x / 32;
	float viewZ = lerp(0.02, 1.0, float(id.x % 32) / 31.0);
	float3 normal = surface % 2 == 0 ? float3(0, 0, 1) : normalize(float3(1, 0, 1));
	float3 view = float3(sqrt(1.0 - viewZ * viewZ), 0, viewZ);
	float materialRoughness = lerp(0.04, 1.0, float(surface / 4) / 7.0);
	PS_INPUT input = (PS_INPUT)0;
	input.WorldPosition = float4(0, 0, 100, 1);
	WetnessSurfaceState wet = CreateWetnessSurfaceState(input, normal, normal, float3(0, 0, 500), view,
		-1000, surface % 4 < 2, 0,
#if !defined(SKIN) && !defined(HAIR)
		float3(0.4, 0.35, 0.3), materialRoughness,
#endif
		1.0, true
#if defined(SKYLIGHTING)
		,
		Skylighting::UNIT_SH
#endif
	);
	Results[id.x * 3] = float4(wet.roughnessSpecular, wet.lightingStrength, wet.surfaceDarkeningMask, wet.rainFilmWeight);
	Results[id.x * 3 + 1] = float4(wet.filmHighlightReduction, wet.glossinessSpecular, wet.highlightReflectanceScale, wet.puddleSkyReflectionScale);
	Results[id.x * 3 + 2] = float4(wet.normal, wet.rainCubemapSuppression);
}

[numthreads(64, 1, 1)] void CheckPuddleSlopes(uint3 id : SV_DispatchThreadID) {
	float angle = float(id.x) * (90.0 / 1023.0);
	float3 meshNormal = float3(sin(radians(angle)), 0, cos(radians(angle)));
	Results[id.x * 3] = float4(Wetterness::GetFlatSurfacePuddleMask(meshNormal), angle,
		Wetterness::GetFlatSurfacePuddleMask(-meshNormal), Wetterness::GetFlatSurfacePuddleMask(float3(0, 0, 0)));
	Results[id.x * 3 + 1] = float4(
		Wetterness::GetGeometricPuddleMask(meshNormal * 1e-12), Wetterness::GetGeometricPuddleMask(meshNormal * 1e12),
		Wetterness::GetGeometricPuddleMask(-meshNormal), Wetterness::GetGeometricPuddleMask(float3(0, 0, 0)));
	float3 positionDx = float3(meshNormal.z, 0, -meshNormal.x);
	float3 positionDy = float3(0, -1, 0);
	Results[id.x * 3 + 2] = float4(Wetterness::GetGeometricPuddleMask(-cross(positionDx, positionDy)), 0, 0, 0);
}

[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID)
{
	const float roughnesses[8] = { 0.005, 0.03, 0.05, 0.10, 0.18, 0.35, 0.70, 1.0 };
	const float roughness = roughnesses[id.x % 8];
	const float viewZ = lerp(0.02, 1.0, float((id.x / 8) % 8) / 7.0);
	const float3 normal = float3(0, 0, 1);
	const float3 view = float3(sqrt(1.0 - viewZ * viewZ), 0, viewZ);
	const float3 light = float3(-view.x, 0, view.z);
	const float rippleMask = float((id.x / 64) % 8) / 7.0;
	const bool isPointLight = id.x >= 512;
	const float darkness = 1.0 - smoothstep(0.06, 0.22, directionalLuminance);
	WetReflectionParams reflection = CreateWetReflectionParams(0.8);
	WetnessDirectLightState state = CreateWetnessDirectLightState(
		normal, normal, view, roughness, saturate(1.0 - roughness), 0.0, reflection, 0.55, rippleMask, darkness, 1.0, true);
	const float3 color = float3(1, 0.25, 0.0625) * lightScale;
#if defined(TRUE_PBR)
	DirectContext context = CreateDirectLightingContext(normal, normal, normal, view, view, light, light, color, 0.5, 0.5);
#else
	DirectContext context = CreateDirectLightingContext(normal, normal, view, light, color, 0.5, 0.5);
#endif
	DirectLightingOutput actual = (DirectLightingOutput)0;
	actual.diffuse = float3(0.4, 0.3, 0.2);
	DirectLightingOutput reference = actual;
#if !defined(WETTERNESS_REFERENCE)
	ApplyWetnessDirectLightingOutput(actual, context, state, isPointLight);
#endif
	const float referenceRoughness = isPointLight ?
	                                     lerp(roughness, max(roughness, 0.18), saturate(rippleMask * darkness)) :
	                                     roughness;
	if (state.enabled > 0.0)
		EvaluateWetnessLighting(normal, context, referenceRoughness, state.params, reference);
#if defined(WETTERNESS_REFERENCE)
	actual = reference;
#endif
	Results[id.x * 3] = float4(actual.specular, isPointLight ? state.pointLightRoughness : state.roughness);
	Results[id.x * 3 + 1] = float4(reference.specular, referenceRoughness);
	Results[id.x * 3 + 2] = float4(
		max(abs(actual.diffuse.x - reference.diffuse.x), max(abs(actual.diffuse.y - reference.diffuse.y), abs(actual.diffuse.z - reference.diffuse.z))),
		state.pointLightHighlightReduction, state.params.lightColorScale, isPointLight ? 1.0 : 0.0);
}

[numthreads(64, 1, 1)] void CheckDistribution(uint3 id : SV_DispatchThreadID) {
	const float roughness = lerp(0.005, 1.0, float(id.x % 32) / 31.0);
	const uint angle = id.x / 32;
	const float NdotH = angle < 16 ? 1.0 - float(angle) * exp2(-24.0) : float(31 - angle) / 16.0;
	Results[id.x * 3] = float4(Wetterness::GetPointLightDistribution(roughness, NdotH), roughness, NdotH, 0);
	const float response = exp2(lerp(-20.0, 28.0, float(angle) / 31.0));
	const float night = float(id.x % 32) / 31.0;
	Results[id.x * 3 + 1] = float4(Wetterness::GetWetHighlightScale(response, night), response, night, 0);
	Results[id.x * 3 + 2] = 0;
}
