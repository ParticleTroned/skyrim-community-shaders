#ifndef LIGHTING_EVAL_HLSLI
#define LIGHTING_EVAL_HLSLI
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#if defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif

#if defined(TRUE_PBR)
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 coatWorldNormal, float3 vertexNormal, float3 viewDir, float3 coatViewDir, float3 lightDir, float3 coatLightDir, float3 lightColor, float detailedShadow, float softShadow)
#else
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir, float3 lightDir, float3 lightColor, float detailedShadow, float softShadow)
#endif
{
	DirectContext context = (DirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	context.lightDir = normalize(lightDir);
	context.halfVector = normalize(context.viewDir + context.lightDir);
	context.lightColor = lightColor;
	context.detailedShadow = detailedShadow;
	context.softShadow = softShadow;
#if defined(TRUE_PBR)
	context.coatWorldNormal = normalize(coatWorldNormal);
	context.coatViewDir = normalize(coatViewDir);
	context.coatLightDir = normalize(coatLightDir);
	context.coatHalfVector = normalize(context.coatViewDir + context.coatLightDir);
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		context.coatLightColor = lightColor * softShadow;
	}
	else
	{
		context.coatLightColor = context.lightColor * detailedShadow;
	}
#endif
	return context;
}

IndirectContext CreateIndirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir)
{
	IndirectContext context = (IndirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	return context;
}

float3 VanillaSpecular(DirectContext context, float shininess, float2 uv, float2 uv_ddx, float2 uv_ddy)
{
	const float3 N = context.worldNormal;
	const float3 G = context.vertexNormal;
	float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;
	float HdotN;
#if defined(ANISO_LIGHTING)
	const float3 AN = normalize(N * 0.5 + G);
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#else
	HdotN = saturate(dot(H, N));
#endif

#if defined(SPECULAR)
	float lightColorMultiplier = exp2(shininess * log2(HdotN));

#elif defined(SPARKLE)
	float lightColorMultiplier = 0;
#else
	float lightColorMultiplier = HdotN;
#endif

#if defined(ANISO_LIGHTING)
	lightColorMultiplier *= 0.7 * max(0, L.z);
#endif

#if defined(SPARKLE) && !defined(SNOW)
	float3 sparkleUvScale = exp2(float3(1.3, 1.6, 1.9) * log2(abs(SparkleParams.x)).xxx);

	float sparkleColor1 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.xx, uv_ddx * sparkleUvScale.x, uv_ddy * sparkleUvScale.x).z;
	float sparkleColor2 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.yy, uv_ddx * sparkleUvScale.y, uv_ddy * sparkleUvScale.y).z;
	float sparkleColor3 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.zz, uv_ddx * sparkleUvScale.z, uv_ddy * sparkleUvScale.z).z;
	float sparkleColor = ProcessSparkleColor(sparkleColor1) + ProcessSparkleColor(sparkleColor2) + ProcessSparkleColor(sparkleColor3);
	float VdotN = dot(V, N);
	V += N * -(2 * VdotN);
	float sparkleMultiplier = exp2(SparkleParams.w * log2(saturate(dot(V, -L)))) * (SparkleParams.z * sparkleColor);
	sparkleMultiplier = sparkleMultiplier >= 0.5 ? 1 : 0;
	lightColorMultiplier += sparkleMultiplier * HdotN;
#endif
	return lightColorMultiplier;
}

void EvaluateLighting(DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv, float2 uv_ddx, float2 uv_ddy, out DirectLightingOutput lightingOutput)
{
	lightingOutput = (DirectLightingOutput)0;
#if defined(TRUE_PBR)
	PBR::GetDirectLightInput(lightingOutput, context, material, tbnTr, uv);
#else
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		Hair::GetHairDirectLight(lightingOutput, context, material, tbnTr, uv);
		return;
	}
#	endif
#	if defined(SKIN) && defined(CS_SKIN)
	if (SharedData::skinData.skinParams.w > 0.0f) {
		Skin::SkinDirectLightInput(lightingOutput, context, material);
		float3 softLightColor = context.lightColor * context.softShadow;

		// SSS fallback for forward skin rendering
#		if !defined(DEFERRED)
		const float NdotL = dot(context.worldNormal, context.lightDir);
#			if defined(SOFT_LIGHTING)
		lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor;
#			endif

#			if defined(RIM_LIGHTING)
		lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor;
#			endif

#			if defined(BACK_LIGHTING)
		lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor;
#			endif
#		endif
		return;
	}
#	endif
	const float NdotL = dot(context.worldNormal, context.lightDir);
	float3 diffuseLightColor = context.lightColor * context.detailedShadow;
	float3 softLightColor = context.lightColor * context.softShadow;
	lightingOutput.diffuse = saturate(NdotL) * diffuseLightColor * Color::VanillaNormalization();
#	if defined(SOFT_LIGHTING)
	lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor * Color::VanillaNormalization();
#	endif

#	if defined(RIM_LIGHTING)
	lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor * Color::VanillaNormalization();
#	endif

#	if defined(BACK_LIGHTING)
	lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor * Color::VanillaNormalization();
#	endif
	lightingOutput.specular = VanillaSpecular(context, material.Shininess, uv, uv_ddx, uv_ddy) * material.SpecularColor * material.Glossiness * diffuseLightColor * Color::VanillaNormalization();
#endif
}

void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, float2 uv)
{
	lobeWeights = (IndirectLobeWeights)0;
#if defined(TRUE_PBR)
	PBR::GetIndirectLobeWeights(lobeWeights, context, material);
#else
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled) {
		Hair::GetHairIndirectLobeWeights(lobeWeights, context, material, uv);
		return;
	}
#	endif
#	if defined(SKIN) && defined(CS_SKIN)
	if (SharedData::skinData.skinParams.w > 0.0f) {
		Skin::SkinIndirectLobeWeights(lobeWeights, material, context);
		return;
	}
#	endif
	lobeWeights.diffuse = material.BaseColor;
#	if defined(DYNAMIC_CUBEMAPS)
	if (any(material.F0 > 0.0)) {
		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;
	}
#	endif
#endif
}

#if defined(WETTERNESS)
struct WetnessDirectLightingParams
{
	float enabled;
	float wetnessF0;
	float wetnessFScale;
	float lightColorScale;
	float NdotV;
};

float3 GetWetReflectionModeConfig(float wetReflectionScale)
{
	const float WET_REFLECTION_SCALE_MAX = 2.0;
	const float legacyReflectionScaleMax = 1.0;
	const float wetReflectionScaleClamped = clamp(wetReflectionScale, 0.0, WET_REFLECTION_SCALE_MAX);
	if (wetReflectionScaleClamped <= 0.0) {
		return 0.0;
	}

	const bool enableModern = SharedData::wetternessSettings.settings.EnableModernWetReflection != 0;
	const bool enableLegacy = SharedData::wetternessSettings.settings.EnableLegacyWetReflection != 0;
	const float modeCount = (enableModern ? 1.0 : 0.0) + (enableLegacy ? 1.0 : 0.0);
	if (modeCount <= 0.0) {
		return 0.0;
	}

	const float invModeCount = rcp(modeCount);
	const float modernScale = wetReflectionScaleClamped;
	const float legacyScale = min(wetReflectionScaleClamped, legacyReflectionScaleMax);
	const float effectiveScale = (modernScale * (enableModern ? 1.0 : 0.0) + legacyScale * (enableLegacy ? 1.0 : 0.0)) * invModeCount;
	return float3(
		enableModern ? invModeCount : 0.0,
		enableLegacy ? invModeCount : 0.0,
		effectiveScale);
}

float GetSkinWetnessOverdriveScale()
{
#	if defined(SKIN) || defined(HAIR)
	const float skinWetnessOverdriveT = saturate((SharedData::wetternessSettings.settings.SkinWetness - 1.0) / 0.5);
	return lerp(1.0, 1.8, skinWetnessOverdriveT);
#	else
	return 1.0;
#	endif
}

WetnessDirectLightingParams CreateWetnessDirectLightingParams(float3 wetnessNormal, float3 viewDir, float roughness, float3 wetReflectionModeConfig)
{
	WetnessDirectLightingParams params = (WetnessDirectLightingParams)0;
	const float wetnessStrength = saturate(1 - roughness);
	if (wetnessStrength <= 0.0) {
		return params;
	}

	const float modernWeight = wetReflectionModeConfig.x;
	const float legacyWeight = wetReflectionModeConfig.y;
	const float wetReflectionScale = wetReflectionModeConfig.z;
	if (wetReflectionScale <= 0.0 || (modernWeight + legacyWeight) <= 0.0) {
		return params;
	}

	const bool forwardReflectionBiasEnabled = SharedData::wetternessSettings.settings.EnableForwardReflectionBias != 0;
	const bool vanillaReflectionCompensationEnabled = SharedData::wetternessSettings.settings.EnableVanillaReflectionCompensation != 0;

	float NdotV = saturate(abs(dot(wetnessNormal, viewDir)) + EPSILON_DOT_CLAMP);
	if (forwardReflectionBiasEnabled) {
		float forwardBiasAmount = saturate((0.62 - NdotV) / 0.62);
		float biasedNdotV = max(NdotV, 0.55);
		NdotV = lerp(NdotV, biasedNdotV, forwardBiasAmount);
	}

	params.enabled = 1.0;
	params.wetnessF0 = 0.02 * modernWeight + wetnessStrength * legacyWeight;
	params.wetnessFScale = wetnessStrength * wetReflectionScale;
	params.lightColorScale = modernWeight + legacyWeight * 0.1;
	params.NdotV = NdotV;
	if (forwardReflectionBiasEnabled) {
		params.wetnessFScale *= 1.18;
	}
#	if !defined(TRUE_PBR)
	if (vanillaReflectionCompensationEnabled) {
		params.lightColorScale *= 1.1;
		params.wetnessFScale *= 1.1;
	}
#	endif
	params.wetnessFScale *= GetSkinWetnessOverdriveScale();

	return params;
}

void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, WetnessDirectLightingParams params, inout DirectLightingOutput lightingOutput)
{
	if (params.enabled <= 0.0) {
		return;
	}

#	if defined(TRUE_PBR)
	float3 lightColor = context.coatLightColor;
#	else
	float3 lightColor = context.lightColor * context.detailedShadow;
#	endif
	lightColor *= params.lightColorScale;

	const float3 N = wetnessNormal;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;

	float NdotL = clamp(dot(N, L), EPSILON_DOT_CLAMP, 1);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(context.viewDir, H));

	float D = BRDF::D_GGX(roughness, NdotH);
	float G = BRDF::Vis_SmithJointApprox(roughness, params.NdotV, NdotL);
	float3 F = BRDF::F_Schlick(params.wetnessF0, VdotH);

	F *= params.wetnessFScale;
	F = saturate(F);

	float3 wetnessSpecular = D * G * F * NdotL * lightColor;

	lightingOutput.diffuse *= 1 - F;
	lightingOutput.specular *= 1 - F;
	lightingOutput.specular += wetnessSpecular;
}

void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, float3 wetReflectionModeConfig, inout DirectLightingOutput lightingOutput)
{
	WetnessDirectLightingParams params = CreateWetnessDirectLightingParams(wetnessNormal, context.viewDir, roughness, wetReflectionModeConfig);
	EvaluateWetnessLighting(wetnessNormal, context, roughness, params, lightingOutput);
}

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, IndirectContext context, float3 wetReflectionModeConfig)
{
	const float wetnessStrength = saturate(1 - roughness);
	if (wetnessStrength <= 0.0) {
		return 0.0;
	}

	const float modernWeight = wetReflectionModeConfig.x;
	const float legacyWeight = wetReflectionModeConfig.y;
	const float wetnessScaleClamped = wetReflectionModeConfig.z;
	if (wetnessScaleClamped <= 0.0 || (modernWeight + legacyWeight) <= 0.0) {
		return 0.0;
	}

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 VN = context.vertexNormal;
	const bool forwardReflectionBiasEnabled = SharedData::wetternessSettings.settings.EnableForwardReflectionBias != 0;
	const bool vanillaReflectionCompensationEnabled = SharedData::wetternessSettings.settings.EnableVanillaReflectionCompensation != 0;

	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	if (forwardReflectionBiasEnabled) {
		float forwardBiasAmount = saturate((0.62 - NdotV) / 0.62);
		float biasedNdotV = max(NdotV, 0.55);
		NdotV = lerp(NdotV, biasedNdotV, forwardBiasAmount);
	}
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 modernLobeWeight = 0.02 * specularBRDF.x + specularBRDF.y;
	float3 legacyLobeWeight = saturate(1.0 - roughness) * specularBRDF.x + specularBRDF.y;
	float glancing = saturate(1.0 - NdotV);
	legacyLobeWeight *= (1.0 + 0.25 * glancing);
	float3 specularLobeWeight = (modernLobeWeight * modernWeight + legacyLobeWeight * legacyWeight) * wetnessStrength * wetnessScaleClamped;
	if (forwardReflectionBiasEnabled) {
		specularLobeWeight *= 1.15;
	}
#	if !defined(TRUE_PBR)
	if (vanillaReflectionCompensationEnabled) {
		specularLobeWeight *= 1.12;
	}
#	endif
	specularLobeWeight *= GetSkinWetnessOverdriveScale();
	specularLobeWeight = saturate(specularLobeWeight);

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	float3 R = reflect(-V, N);
	float horizon = min(1.0 + dot(R, VN), 1.0);
	horizon = horizon * horizon;
	if (forwardReflectionBiasEnabled) {
		horizon = max(horizon, 0.45);
	}
	if (vanillaReflectionCompensationEnabled) {
		horizon = max(horizon, 0.35);
	}
	specularLobeWeight *= horizon;

	return specularLobeWeight;
}
#elif defined(WETNESS_EFFECTS)
void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, inout DirectLightingOutput lightingOutput)
{
	const float wetnessStrength = saturate(1 - roughness);
#	if defined(TRUE_PBR)
	const float3 lightColor = context.coatLightColor;
#	else
	const float3 lightColor = context.lightColor * context.detailedShadow;
#	endif

	const float wetnessF0 = 0.02;

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;

	float NdotL = clamp(dot(N, L), EPSILON_DOT_CLAMP, 1);
	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	float D = BRDF::D_GGX(roughness, NdotH);
	float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
	float3 F = BRDF::F_Schlick(wetnessF0, VdotH);

	// Separate physical Fresnel from effective contribution weighted by strength
	float3 wetnessF = F * wetnessStrength;

	float3 wetnessSpecular = D * G * wetnessF * NdotL * lightColor;

#	if !defined(TRUE_PBR)
	wetnessSpecular *= Color::PBRLightingCompensation * Color::PBRLightingScale;  // Compensate for GGX on traditional specular
#	endif

	lightingOutput.diffuse *= 1 - wetnessF;
	lightingOutput.specular *= 1 - wetnessF;
	lightingOutput.specular += wetnessSpecular;
}

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, IndirectContext context)
{
	const float wetnessF0 = 0.02;
	const float wetnessStrength = saturate(1 - roughness);

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;

	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 specularLobeWeight = wetnessF0 * specularBRDF.x + specularBRDF.y;

	specularLobeWeight *= wetnessStrength;

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	return specularLobeWeight;
}
#endif
#endif
