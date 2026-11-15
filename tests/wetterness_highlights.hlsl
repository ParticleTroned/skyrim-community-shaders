#define main LightingMain
#include "Lighting.hlsl"
#undef main

cbuffer TestInputs : register(b12)
{
	float directionalLuminance;
	float lightScale;
};
RWStructuredBuffer<float4> Results : register(u0);

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
		normal, normal, view, roughness, reflection, 0.55, rippleMask, darkness, 1.0, true);
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
	Results[id.x * 3 + 1] = float4(Wetterness::GetPointLightHighlightScale(response, night), response, night, 0);
	Results[id.x * 3 + 2] = 0;
}
