#ifndef LIGHTING_EVAL_HLSLI
#define LIGHTING_EVAL_HLSLI
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#if defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif

float3 SafeNormalizeLighting(float3 v, float3 fallback)
{
	float lenSq = dot(v, v);
	return (lenSq > EPSILON_DIVISION && lenSq == lenSq && lenSq < 1.0e16) ? v * rsqrt(lenSq) : fallback;
}

#if defined(TRUE_PBR)
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 coatWorldNormal, float3 vertexNormal, float3 viewDir, float3 coatViewDir, float3 lightDir, float3 coatLightDir, float3 lightColor, float detailedShadow, float softShadow)
#else
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir, float3 lightDir, float3 lightColor, float detailedShadow, float softShadow)
#endif
{
	DirectContext context = (DirectContext)0;
	context.worldNormal = SafeNormalizeLighting(worldNormal, float3(0.0, 0.0, 1.0));
	context.vertexNormal = SafeNormalizeLighting(vertexNormal, context.worldNormal);
	context.viewDir = SafeNormalizeLighting(viewDir, context.worldNormal);
	context.lightDir = SafeNormalizeLighting(lightDir, context.worldNormal);
	context.halfVector = SafeNormalizeLighting(context.viewDir + context.lightDir, context.worldNormal);
	context.lightColor = lightColor;
	context.detailedShadow = detailedShadow;
	context.softShadow = softShadow;
#if defined(TRUE_PBR)
	context.coatWorldNormal = SafeNormalizeLighting(coatWorldNormal, context.worldNormal);
	context.coatViewDir = SafeNormalizeLighting(coatViewDir, context.coatWorldNormal);
	context.coatLightDir = SafeNormalizeLighting(coatLightDir, context.coatWorldNormal);
	context.coatHalfVector = SafeNormalizeLighting(context.coatViewDir + context.coatLightDir, context.coatWorldNormal);
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
	context.worldNormal = SafeNormalizeLighting(worldNormal, float3(0.0, 0.0, 1.0));
	context.vertexNormal = SafeNormalizeLighting(vertexNormal, context.worldNormal);
	context.viewDir = SafeNormalizeLighting(viewDir, context.worldNormal);
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
	const float3 AN = SafeNormalizeLighting(N * 0.5 + G, N);
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
	lobeWeights.diffuse = material.BaseColor;
#	if defined(DYNAMIC_CUBEMAPS)
	if (any(material.F0 > 0)) {
		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;
		lobeWeights.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		lobeWeights.specular *= horizon;
	}
#	endif
#endif
}

#if defined(WETTERNESS)
struct WetReflectionParams
{
	float modernWeight;
	float legacyWeight;
	float effectiveScale;
	float forwardBiasEnabled;
	float vanillaCompensationEnabled;
	float skinOverdriveScale;
};

struct WetnessDirectLightingParams
{
	float wetnessF0;
	float wetnessFScale;
	float lightColorScale;
	float NdotV;
};

float GetSkinWetnessOverdriveScale()
{
#	if defined(SKIN) || defined(HAIR)
	// Preserve exact legacy output for SkinWetness <= 1.0 and only add extra intensity
	// in the extended slider segment (1.0 .. 1.5).
	const float skinWetnessOverdriveT = saturate((SharedData::wetternessSettings.SkinWetness - 1.0) / 0.5);
	return lerp(1.0, 1.8, skinWetnessOverdriveT);
#	else
	return 1.0;
#	endif
}

WetReflectionParams CreateWetReflectionParams(float wetReflectionScale)
{
	WetReflectionParams params = (WetReflectionParams)0;

	const float WET_REFLECTION_SCALE_MAX = 2.0;
	const float legacyReflectionScaleMax = 1.0;
	const float wetReflectionScaleClamped = clamp(wetReflectionScale, 0.0, WET_REFLECTION_SCALE_MAX);
	if (wetReflectionScaleClamped <= 0.0) {
		return params;
	}

	const float enableModern = SharedData::wetternessSettings.EnableModernWetReflection != 0 ? 1.0 : 0.0;
	const float enableLegacy = SharedData::wetternessSettings.EnableLegacyWetReflection != 0 ? 1.0 : 0.0;
	const float modeCount = enableModern + enableLegacy;
	if (modeCount <= 0.0) {
		return params;
	}

	const float invModeCount = rcp(modeCount);
	const float modernScale = wetReflectionScaleClamped;
	const float legacyScale = min(wetReflectionScaleClamped, legacyReflectionScaleMax);

	params.modernWeight = enableModern * invModeCount;
	params.legacyWeight = enableLegacy * invModeCount;
	params.effectiveScale = (modernScale * enableModern + legacyScale * enableLegacy) * invModeCount;
	params.forwardBiasEnabled = SharedData::wetternessSettings.EnableForwardReflectionBias != 0 ? 1.0 : 0.0;
	params.vanillaCompensationEnabled = SharedData::wetternessSettings.EnableVanillaReflectionCompensation != 0 ? 1.0 : 0.0;
	params.skinOverdriveScale = GetSkinWetnessOverdriveScale();

	return params;
}

bool HasWetReflectionParams(WetReflectionParams params)
{
	return params.effectiveScale > 1e-4 &&
	       (params.modernWeight + params.legacyWeight) > 0.0;
}

float GetWetnessBiasedNdotV(float3 wetnessNormal, float3 viewDir, float forwardBiasEnabled)
{
	// Shared by direct and indirect wet reflection so their forward/top-down bias stays matched.
	float NdotV = saturate(abs(dot(wetnessNormal, viewDir)) + EPSILON_DOT_CLAMP);
	float forwardBiasAmount = saturate((0.62 - NdotV) / 0.62) * forwardBiasEnabled;
	float biasedNdotV = max(NdotV, 0.55);
	return lerp(NdotV, biasedNdotV, forwardBiasAmount);
}

WetnessDirectLightingParams CreateWetnessDirectLightingParams(float3 wetnessNormal, float3 viewDir, float wetnessStrength, WetReflectionParams reflectionParams)
{
	WetnessDirectLightingParams params = (WetnessDirectLightingParams)0;
	float NdotV = GetWetnessBiasedNdotV(wetnessNormal, viewDir, reflectionParams.forwardBiasEnabled);

	params.wetnessF0 = 0.02 * reflectionParams.modernWeight + wetnessStrength * reflectionParams.legacyWeight;
	params.wetnessFScale = wetnessStrength * reflectionParams.effectiveScale;
	params.lightColorScale = reflectionParams.modernWeight + reflectionParams.legacyWeight * 0.1;
	params.NdotV = NdotV;
	params.wetnessFScale *= lerp(1.0, 1.18, reflectionParams.forwardBiasEnabled);
#	if !defined(TRUE_PBR)
	params.lightColorScale *= lerp(1.0, 1.1, reflectionParams.vanillaCompensationEnabled);
	params.wetnessFScale *= lerp(1.0, 1.1, reflectionParams.vanillaCompensationEnabled);
#	endif
	params.wetnessFScale *= reflectionParams.skinOverdriveScale;

	return params;
}

void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, WetnessDirectLightingParams params, inout DirectLightingOutput lightingOutput)
{
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

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, float wetnessStrength, IndirectContext context, WetReflectionParams reflectionParams)
{
	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 VN = context.vertexNormal;

	float NdotV = GetWetnessBiasedNdotV(N, V, reflectionParams.forwardBiasEnabled);
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 modernLobeWeight = 0.02 * specularBRDF.x + specularBRDF.y;
	float3 legacyLobeWeight = saturate(1.0 - roughness) * specularBRDF.x + specularBRDF.y;
	float glancing = saturate(1.0 - NdotV);
	legacyLobeWeight *= (1.0 + 0.25 * glancing);
	float3 specularLobeWeight = (modernLobeWeight * reflectionParams.modernWeight + legacyLobeWeight * reflectionParams.legacyWeight) * wetnessStrength * reflectionParams.effectiveScale;
	specularLobeWeight *= lerp(1.0, 1.15, reflectionParams.forwardBiasEnabled);
#	if !defined(TRUE_PBR)
	specularLobeWeight *= lerp(1.0, 1.12, reflectionParams.vanillaCompensationEnabled);
#	endif
	specularLobeWeight *= reflectionParams.skinOverdriveScale;
	specularLobeWeight = saturate(specularLobeWeight);

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	// Horizon specular occlusion
	// https://marmosetco.tumblr.com/post/81245981087
	float3 R = reflect(-V, N);
	float horizon = min(1.0 + dot(R, VN), 1.0);
	horizon = horizon * horizon;
	horizon = lerp(horizon, max(horizon, 0.45), reflectionParams.forwardBiasEnabled);
	horizon = lerp(horizon, max(horizon, 0.35), reflectionParams.vanillaCompensationEnabled);
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
	wetnessSpecular *= Color::PBRLightingCompensation * Color::PBRLightingScale;
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
	const float3 VN = context.vertexNormal;

	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 specularLobeWeight = wetnessF0 * specularBRDF.x + specularBRDF.y;

	specularLobeWeight *= wetnessStrength;

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	float3 R = reflect(-V, N);
	float horizon = min(1.0 + dot(R, VN), 1.0);
	horizon = horizon * horizon;
	specularLobeWeight *= horizon;

	return specularLobeWeight;
}
#endif
#endif
