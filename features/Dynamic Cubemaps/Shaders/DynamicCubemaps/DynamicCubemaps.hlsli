#ifndef DYNAMICCUBEMAPS_HLSLI
#define DYNAMICCUBEMAPS_HLSLI

#include "Common/BRDF.hlsli"

#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

namespace DynamicCubemaps
{
	TextureCube<float3> EnvReflectionsTexture : register(t30);
	TextureCube<float3> EnvTexture : register(t31);

#if !defined(WATER)

#	if defined(IBL) && defined(LIGHTING)
	bool ShouldUseStaticIBL()
	{
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		return SharedData::iblSettings.EnableStaticIBL && !inWorld && !inReflection;
	}
#	endif

	float3 ComputeSpecularIrradiance(float3 R, float level, float directionalAmbientColorSpecular, float skylightingSpecular)
	{
#	if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			float3 envSpecular = 0.0;
			float3 skySpecular = 0.0;
			ImageBasedLighting::ComputeSpecularIBL(
				EnvTexture,
				EnvReflectionsTexture,
				SampColorSampler,
				R,
				level,
				directionalAmbientColorSpecular,
				skylightingSpecular,
				envSpecular,
				skySpecular);
			return envSpecular + skySpecular;
		}
#	endif

		// Fallback without IBL: normalize-by-luminance with DALC
#	if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			return Color::IrradianceToLinear(specularIrradiance);
		}

		float3 specularIrradianceReflections = 0.0;
		if (skylightingSpecular > 0.0) {
			specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
			specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
			specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
		}

		float3 specularIrradiance = 0.0;
		if (skylightingSpecular < 1.0) {
			specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
			float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
			float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skylightingSpecular);
			specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
			specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
		}
		return lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#	else
		float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
		float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
		specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
		return Color::IrradianceToLinear(specularIrradiance);
#	endif
	}

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness, sh2 skylighting)
#	else
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness)
#	endif
	{
#	if defined(DEFERRED)
		return 1.0;
#	else
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float3 finalIrradiance = 0;

		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(
													max(0, SharedData::GetAmbient(R)))) *
		                                        Color::ReflectionNormalisationScale;
		float skylightingSpecular = 1.0;

#		if defined(IBL) && defined(LIGHTING)
		const bool useStaticIBL = ShouldUseStaticIBL();
#		else
		const bool useStaticIBL = false;
#		endif

		if (!useStaticIBL) {
#		if defined(SKYLIGHTING)
			if (!SharedData::InInterior) {
				skylightingSpecular = Skylighting::EvaluateSpecular(skylighting, SphericalHarmonics::FauxSpecularLobe(N, V, roughness));
			}
#		endif
			finalIrradiance = ComputeSpecularIrradiance(R, level, directionalAmbientColorSpecular, skylightingSpecular);
		} else {
#		if defined(IBL) && defined(LIGHTING)
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance = specularIrradiance;
#		endif
		}

		return finalIrradiance;
#	endif
	}

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemap(float3 N, float3 V, float roughness, float3 F0, sh2 skylighting)
#	else
	float3 GetDynamicCubemap(float3 N, float3 V, float roughness, float3 F0)
#	endif
	{
#	if defined(DEFERRED)
		return 1.0;
#	else
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float2 specularBRDF = BRDF::EnvBRDF(roughness, NoV);

		float3 finalIrradiance = 0;
		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(max(0, SharedData::GetAmbient(R)))) * Color::ReflectionNormalisationScale;
		float skylightingSpecular = 1.0;

#		if defined(IBL) && defined(LIGHTING)
		if (ShouldUseStaticIBL()) {
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			return (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;
		}
#		endif

#		if defined(SKYLIGHTING)
		if (!SharedData::InInterior) {
			skylightingSpecular = Skylighting::EvaluateSpecular(skylighting, SphericalHarmonics::FauxSpecularLobe(N, V, roughness));
		}
#		endif

		finalIrradiance = ComputeSpecularIrradiance(R, level, directionalAmbientColorSpecular, skylightingSpecular);

		return (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
#	endif
	}
#endif  // !WATER
}
#endif  // DYNAMICCUBEMAPS_HLSLI
