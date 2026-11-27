#ifndef __SKIN_HLSLI__
#define __SKIN_HLSLI__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/LightingCommon.hlsli"
#include "Common/Math.hlsli"
#include "Common/NormalBlending.hlsli"
#include "Common/Random.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"

namespace Skin
{
	cbuffer SkinPerGeometry : register(b7)
	{
		float4 skinPerGeometry;
		float4 skinMaterialFlags;
	};

	SharedData::SkinData GetSkinData() { return SharedData::skinData; }
	bool IsEnabled() { return SharedData::skinData.skinParams.w > 0 && skinMaterialFlags.w >= 0; }

#if defined(CS_SKIN_SHADING)
	Texture2D<float4> TexSkinDetailNormal : register(t72);
	Texture2D<float4> TexSkinWetness : register(t74);
	Texture2D<float4> TexSkinExtra : register(t75);
	Texture2D<float4> TexSkinThickness : register(t99);
	SamplerState DetailSampler : register(s7);

	float3 NormalFromHeight(float height, float scale, float2 uv)
	{
		float2 dx = ddx(uv), dy = ddy(uv);
		float hx = ddx(height), hy = ddy(height);
		float determinant = dx.x * dy.y - dx.y * dy.x;
		if (abs(determinant) < 1e-8f)
			return float3(0, 0, 1);
		return float3(-(hx * dy.y - hy * dx.y), -(hy * dx.x - hx * dy.x), 0) * (scale / determinant) + float3(0, 0, 1);
	}

	float3x3 SurfaceBasis(float3 position, float3 normal, float2 uv)
	{
		float3 dx = ddx(position), dy = ddy(position);
		float2 du = ddx(uv), dv = ddy(uv);
		float determinant = du.x * dv.y - du.y * dv.x;
		float signUV = determinant < 0 ? -1.0f : 1.0f;
		float3 tangent = (dx * dv.y - dy * du.y) * signUV;
		tangent -= normal * dot(normal, tangent);
		float3 fallbackAxis = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0);
		if (abs(determinant) < 1e-12f || dot(tangent, tangent) < 1e-12f)
			tangent = cross(fallbackAxis, normal);
		tangent = normalize(tangent);
		float3 bitangent = (dy * du.x - dx * dv.x) * signUV;
		float handedness = dot(cross(normal, tangent), bitangent) < 0 ? -1.0f : 1.0f;
		return float3x3(tangent, cross(normal, tangent) * handedness, normal);
	}

	// [Jorge Jimenez, Diego Gutierrez 2015, "Separable Subsurface Scattering"]
	// https://www.iryoku.com/separable-sss/
	/// Evaluates the light-independent thickness profile for direct transmission.
	float3 SSSSTransmittanceProfile(float translucency, float sssWidth, float d)
	{
		float scale = 8.25 * (1.0 - translucency) / max(sssWidth, 1e-4f);
		d = scale * abs(d);
		float dd = -d * d;
		float3 profile = float3(0.233, 0.455, 0.649) * exp(dd / 0.0064) +
		                 float3(0.1, 0.336, 0.344) * exp(dd / 0.0484) +
		                 float3(0.118, 0.198, 0.0) * exp(dd / 0.187) +
		                 float3(0.113, 0.007, 0.007) * exp(dd / 0.567) +
		                 float3(0.358, 0.004, 0.0) * exp(dd / 1.99) +
		                 float3(0.078, 0.0, 0.0) * exp(dd / 7.41);

		return profile;
	}

	// Filament-style compensation using a fit to the F = 1 integral of D_GGX * Vis_SmithJointApprox.
	float3 GGXEnergyCompensation(float roughness, float NdotV, float3 F0)
	{
		float a = roughness * roughness;
		float t = NdotV * rcp(NdotV + 1.103665f * a);
		float x = 2.0f * t - 1.0f;
		float grazingLoss = 1.424916f + x * (0.341704f + x * (-0.646581f - 0.742605f * x)) -
		                    roughness * (0.847600f + 1.693598f * x);
		float roughnessLoss = 1.841562f + 0.210406f * NdotV - 0.899554f * roughness;
		float singleScatterAlbedo = saturate(1.0f - t * ((1.0f - t) * grazingLoss + a * a * roughnessLoss));
		return 1.0f + F0 * (rcp(max(singleScatterAlbedo, 1e-4f)) - 1.0f);
	}

	float3 DualSpecularGGX(float AverageRoughness, float Lobe0Roughness, float Lobe1Roughness, float LobeMix, float3 SpecularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = lerp(BRDF::D_GGX(Lobe0Roughness, NdotH), BRDF::D_GGX(Lobe1Roughness, NdotH), LobeMix);
		float G = BRDF::Vis_SmithJointApprox(AverageRoughness, NdotV, NdotL);
		F = BRDF::F_Schlick(SpecularColor, VdotH);

		return D * G * F;
	}

	void SkinDirectLightInput(
		out DirectLightingOutput lightingOutput,
		DirectContext context,
		MaterialProperties material)
	{
		lightingOutput = (DirectLightingOutput)0;
		context.lightColor *= Color::PBRLightingCompensation * context.detailedShadow;
		[branch] if (all(context.lightColor == 0.0))
		{
			return;
		}

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;
		const float3 H = context.halfVector;

		const float oNdotL = dot(N, L);
		float NdotL = clamp(oNdotL, 1e-5, 1.0);
		float NdotV = saturate(abs(dot(N, V)) + 1e-5);
		float NdotH = saturate(dot(N, H));
		float VdotH = saturate(dot(V, H));

		float averageRoughness = lerp(material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity);

		lightingOutput.diffuse += context.lightColor * NdotL * BRDF::Diffuse_Burley(averageRoughness, NdotV, NdotL, VdotH);
		float3 F;
		float3 F0 = material.F0;

		lightingOutput.specular += DualSpecularGGX(averageRoughness, material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity, F0, NdotL, NdotV, NdotH, VdotH, F) * context.lightColor * NdotL;

		lightingOutput.specular *= material.DirectSpecularEnergyCompensation;
		lightingOutput.diffuse *= 1 - F;

		if (material.FuzzWeight > 0.0) {
			float3 FuzzF0 = material.FuzzColor;
			float fuzzD = BRDF::D_Charlie(material.FuzzRoughness, NdotH);
			float fuzzG = BRDF::Vis_Neubelt(NdotV, NdotL);
			float3 fuzzF = BRDF::F_Schlick(FuzzF0, VdotH);
			float3 fuzzSpecular = fuzzD * fuzzG * fuzzF * context.lightColor * NdotL;
			float2 fuzzSpecularBRDF = BRDF::EnvBRDFApproxLazarov(material.FuzzRoughness, NdotV);
			fuzzSpecular *= 1 + material.FuzzColor * (1 / (fuzzSpecularBRDF.x + fuzzSpecularBRDF.y) - 1);

			float3 retainedEnergy = 1.0f - saturate(fuzzF * material.FuzzWeight);
			lightingOutput.diffuse *= retainedEnergy;
			lightingOutput.specular *= retainedEnergy;
			lightingOutput.specular += fuzzSpecular * material.FuzzWeight;
		}

		SharedData::SkinData skinData = GetSkinData();
		[branch] if (skinData.sssParams.w > 0 && skinData.sssParams.x > 0)
		{
			float3 transmitted = (material.TransmissionProfile * saturate(0.3 + dot(L, -N))) * skinData.sssParams.x;
			lightingOutput.transmission = min(transmitted * context.lightColor * context.softShadow * material.BaseColor, context.lightColor);
		}
	}

	void SkinIndirectLobeWeights(
		out IndirectLobeWeights lobeWeights,
		MaterialProperties material,
		IndirectContext context)
	{
		lobeWeights = (IndirectLobeWeights)0;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float averageRoughness = lerp(material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity);

		float2 specularBRDF = BRDF::EnvBRDF(averageRoughness, NdotV);

		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;

		lobeWeights.diffuse = material.BaseColor * (1.0 - saturate(lobeWeights.specular));
		lobeWeights.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon;
		lobeWeights.specular *= horizon;

		float3 diffuseAO = material.AO;
		float3 specularAO = SpecularAOLagarde(NdotV, material.AO, averageRoughness);

		diffuseAO = MultiBounceAO(material.BaseColor, diffuseAO.x).y;
		specularAO = MultiBounceAO(material.F0, specularAO.x).y;

		lobeWeights.diffuse *= diffuseAO;
		lobeWeights.specular *= specularAO;
	}

	float FBM(float2 uv, float base_scale, int octaves, float lacunarity, float persistence, float z_offset_multiplier)
	{
		float total = 0.0;
		float frequency = base_scale;
		float amplitude = 1.0;
		float max_amplitude = 0.0;
		for (int i = 0; i < octaves; i++) {
			total += amplitude * (Random::perlinNoise(float3(uv * frequency, (float)i * z_offset_multiplier)) + 1.0) * 0.5;

			max_amplitude += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		if (max_amplitude > 0.0) {
			return total / max_amplitude;
		}
		return 0.0;
	}

	float PerlinNoise(float2 uv, float scale, float lacunarity, float persistence, float strength)
	{
		if (strength <= 0.001f) {
			return 0.0f;
		}
		if (strength >= 0.999f) {
			return 1.0f;
		}
		int octaves = 5;
		float z_offset_multiplier = 7.375f;

		float noise_value = FBM(uv, scale, octaves, lacunarity, persistence, z_offset_multiplier);

		float dynamic_threshold = 1.0f - strength;

		float sweat_intensity = saturate((noise_value - dynamic_threshold) / strength);

		sweat_intensity = pow(abs(sweat_intensity), 1.5f);

		if (strength > 0.8f) {
			sweat_intensity = sweat_intensity * saturate(0.99f - (strength - 0.8f) * 5.0f) + (strength - 0.8f) * 5.0f;
		}
		return pow(abs(sweat_intensity), 0.1f);
	}
#endif

	float2 GetWetness(float z, float3 modelNormal)
	{
		if (skinPerGeometry.x == 0.f && skinPerGeometry.y == 0.f)
			return 0.f;

		float waterWet = 0.0f;
		float waterLevel = skinPerGeometry.z + skinPerGeometry.w;

		waterWet = skinPerGeometry.y * (1 - smoothstep(waterLevel - 2.5f, waterLevel + 2.5f, z));

		float sweatWet = skinPerGeometry.x;
#if !defined(SKIN)
		sweatWet *= 1.0f - saturate(dot(modelNormal, float3(0, 0, 1)));
#endif
		return float2(sweatWet, waterWet);
	}
}

#endif  // __SKIN_HLSLI__
