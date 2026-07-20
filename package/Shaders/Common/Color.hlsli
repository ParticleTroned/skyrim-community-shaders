#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/PointLightFlags.hlsli"
#include "Common/SharedData.hlsli"

#if defined(LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS)
#	include "Common/Permutation.hlsli"
#endif

#define ENABLE_LL SharedData::linearLightingSettings.enableLinearLighting
#define ENABLE_ADAPTIVE_BRIGHTNESS_COLOR_ADJUSTMENTS SharedData::linearLightingSettings.enableAdaptiveBrightnessColorAdjustments

// Object shaders opt into this so LL material/effect adjustments skip menu and
// out-of-world draws while image-space scene passes keep the global LL state.
#if defined(PSHADER) && defined(LL_COLOR_ADJUSTMENTS_USE_EXTRA_FLAGS)
#	define ENABLE_LL_COLOR_ADJUSTMENTS \
		(ENABLE_LL && ((Permutation::ExtraShaderDescriptor & (Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::InReflection)) != 0))
#	define ENABLE_ADAPTIVE_BRIGHTNESS_ADJUSTMENTS \
		(ENABLE_ADAPTIVE_BRIGHTNESS_COLOR_ADJUSTMENTS && ((Permutation::ExtraShaderDescriptor & (Permutation::ExtraFlags::InWorld | Permutation::ExtraFlags::InReflection)) != 0))
#else
#	define ENABLE_LL_COLOR_ADJUSTMENTS ENABLE_LL
#	define ENABLE_ADAPTIVE_BRIGHTNESS_ADJUSTMENTS ENABLE_ADAPTIVE_BRIGHTNESS_COLOR_ADJUSTMENTS
#endif

// Adaptive Brightness shares Linear Lighting's settings payload, but it must
// not enable Linear Lighting's color-space conversion path.
#define ENABLE_BRIGHTNESS_ADJUSTMENTS (ENABLE_LL_COLOR_ADJUSTMENTS || ENABLE_ADAPTIVE_BRIGHTNESS_ADJUSTMENTS)

#if defined(PSHADER) && defined(LIGHTING)
cbuffer LLPerGeometry : register(b8)
{
	float emissiveMult;
	float3 pad0;
};
#endif

#if defined(PSHADER) && defined(CS_UTILITY) && !defined(LIGHT_LIMIT_FIX)
cbuffer CSUtilityPerGeometry : register(b3)
{
	uint4 CSUtilityPointLightFlags0;
	uint4 CSUtilityPointLightFlags1;
};
#endif

// Float limits
#define FLT_MIN asfloat(0x00800000)  // 1.175494351e-38f
#define FLT_MAX asfloat(0x7F7FFFFF)  // 3.402823466e+38f

namespace Color
{
	static float GammaCorrectionValue = 2.2;
	static const uint PointLightFlagLinear = POINT_LIGHT_FLAG_LINEAR;
	static const uint PointLightFlagSpot = POINT_LIGHT_FLAG_SPOT;
	static const uint PointLightFlagOmnidirectionalBulb = POINT_LIGHT_FLAG_OMNIDIRECTIONAL;
	static const uint PackedPointLightFlagVectorSize = 4;
	static const uint MaxVanillaPointLightFlags = 8;

	// Copyright 2019 Google LLC.
	// SPDX-License-Identifier: Apache-2.0
	// Polynomial approximation in GLSL for the Turbo colormap
	// Original LUT: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
	// Authors: Anton Mikhailov (mikhailov@google.com), Ruofei Du (ruofei@google.com)
	// https://ai.googleblog.com/2019/08/turbo-improved-rainbow-colormap-for.html
	float3 TurboColormap(float x)
	{
		const float4 kRedVec4 = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
		const float4 kGreenVec4 = float4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
		const float4 kBlueVec4 = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
		const float2 kRedVec2 = float2(-152.94239396, 59.28637943);
		const float2 kGreenVec2 = float2(4.27729857, 2.82956604);
		const float2 kBlueVec2 = float2(-89.90310912, 27.34824973);

		x = saturate(x);
		float4 v4 = float4(1.0, x, x * x, x * x * x);
		float2 v2 = v4.zw * v4.z;
		return float3(
			dot(v4, kRedVec4) + dot(v2, kRedVec2),
			dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
			dot(v4, kBlueVec4) + dot(v2, kBlueVec2));
	}

	float RGBToLuminance(float3 color)
	{
		return dot(color, float3(0.2125, 0.7154, 0.0721));
	}

	float RGBToLuminanceAlternative(float3 color)
	{
		return dot(color, float3(0.3, 0.59, 0.11));
	}

	float RGBToLuminance2(float3 color)
	{
		return dot(color, float3(0.299, 0.587, 0.114));
	}

	float3 RGBToYCoCg(float3 color)
	{
		float tmp = 0.25 * (color.r + color.b);
		return float3(
			tmp + 0.5 * color.g,        // Y
			0.5 * (color.r - color.b),  // Co
			-tmp + 0.5 * color.g        // Cg
		);
	}

	float3 YCoCgToRGB(float3 color)
	{
		float tmp = color.x - color.z;
		return float3(
			tmp + color.y,
			color.x + color.z,
			tmp - color.y);
	}

	float3 Saturation(float3 color, float saturation)
	{
		float grey = RGBToLuminance(color);
		color.x = max(lerp(grey, color.x, saturation), 0.0f);
		color.y = max(lerp(grey, color.y, saturation), 0.0f);
		color.z = max(lerp(grey, color.z, saturation), 0.0f);
		return color;
	}

	float SkyrimGammaToLinear(float color)
	{
		return pow(abs(color), 1.6);
	}

	float LinearToSkyrimGamma(float color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 SkyrimGammaToLinear(float3 color)
	{
		return pow(abs(color), 1.6);
	}

	float3 LinearToSkyrimGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 SrgbToLinear(float3 color)
	{
		return pow(abs(color), 2.2);
	}

	float3 LinearToSrgb(float3 color)
	{
		return pow(abs(color), 1.0 / 2.2);
	}

	float3 GammaToLinearSafe(float3 color)
	{
		return sign(color) * pow(abs(color), 2.2);
	}

	float3 LinearToGammaSafe(float3 color)
	{
		return sign(color) * pow(abs(color), 1.0 / 2.2);
	}

	bool UseLinearLightingColorAdjustments()
	{
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
		return ENABLE_LL_COLOR_ADJUSTMENTS;
#else
		return false;
#endif
	}

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	// Attempt to match vanilla materials that are darker than PBR
	const static float PBRLightingScale = ENABLE_LL_COLOR_ADJUSTMENTS ? 1.0 : 0.65;

	// Attempt to normalise reflection brightness against DALC
	const static float ReflectionNormalisationScale = ENABLE_LL_COLOR_ADJUSTMENTS ? 1.0 : 1.0;

	const static float PBRLightingCompensation = ENABLE_LL_COLOR_ADJUSTMENTS ? 1.0 : Math::PI;

	// Linear Lighting Functions
	float3 LLGammaToLinear(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? SkyrimGammaToLinear(color) : color;
	}

	float3 LLLinearToGamma(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? LinearToSkyrimGamma(color) : color;
	}

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : LinearToSrgb(color);
#	else
		return ENABLE_LL_COLOR_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.colorGamma) * SharedData::linearLightingSettings.vanillaDiffuseColorMult : color;
#	endif
	}

	float3 Light(float3 color, bool isLinear = false)
	{
		color = (ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? pow(abs(color), SharedData::linearLightingSettings.lightGamma) : color;
#	if defined(TRUE_PBR)
		return color * PBRLightingCompensation;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}

	float3 DirectionalLight(float3 color, bool isLinear = false)
	{
		return Light(color, isLinear) *
		       ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
		       SharedData::csUtilitySettings.directionalLightMult;
	}

	float GetPointLightMultiplier(bool isLinear)
	{
		return (ENABLE_LL_COLOR_ADJUSTMENTS && isLinear) ? SharedData::csUtilitySettings.linearPointLightMult : SharedData::csUtilitySettings.pointLightMult;
	}

	float GetPointLightTypeMultiplier(bool isLinear, uint lightFlags)
	{
		const bool useLinearMultiplier = ENABLE_LL_COLOR_ADJUSTMENTS && isLinear;
		if ((lightFlags & PointLightFlagSpot) != 0)
			return useLinearMultiplier ? SharedData::csUtilitySettings.linearSpotlightMult : SharedData::csUtilitySettings.spotlightMult;
		if ((lightFlags & PointLightFlagOmnidirectionalBulb) != 0)
			return useLinearMultiplier ? SharedData::csUtilitySettings.linearOmnidirectionalBulbMult : SharedData::csUtilitySettings.omnidirectionalBulbMult;
		return 1.0f;
	}

	float3 PointLight(float3 color, bool isLinear = false, uint lightFlags = 0)
	{
		return Light(color, isLinear) *
		       ((ENABLE_LL_COLOR_ADJUSTMENTS && !isLinear) ? Math::PI : 1.0f) *
		       GetPointLightMultiplier(isLinear) *
		       GetPointLightTypeMultiplier(isLinear, lightFlags);
	}

	uint GetVanillaPointLightFlags(uint lightIndex)
	{
#	if defined(PSHADER) && defined(CS_UTILITY) && !defined(LIGHT_LIMIT_FIX)
		if (lightIndex >= MaxVanillaPointLightFlags)
			return 0;
		return lightIndex < PackedPointLightFlagVectorSize ? CSUtilityPointLightFlags0[lightIndex] : CSUtilityPointLightFlags1[lightIndex - PackedPointLightFlagVectorSize];
#	else
		return 0;
#	endif
	}
#	if defined(LIGHTING)
	float3 EmitColor(float3 color)
	{
		if (ENABLE_LL_COLOR_ADJUSTMENTS) {
			return pow(abs(color / max(emissiveMult, 1e-5)), SharedData::linearLightingSettings.emitColorGamma) * emissiveMult * SharedData::linearLightingSettings.emitColorMult;
		}

		// Adaptive Brightness only adjusts the multiplier. Keep the engine-authored
		// emissive value intact so this path does not depend on LLPerGeometry.
		return ENABLE_ADAPTIVE_BRIGHTNESS_ADJUSTMENTS ? color * SharedData::linearLightingSettings.emitColorMult : color;
	}
#	endif

	float3 Glowmap(float3 color)
	{
#	if defined(TRUE_PBR)
		color = ENABLE_LL_COLOR_ADJUSTMENTS ? color : LinearToSrgb(color);
#	else
		if (ENABLE_LL_COLOR_ADJUSTMENTS) {
			color = pow(abs(color), SharedData::linearLightingSettings.glowmapGamma);
		}
#	endif

		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? color * SharedData::linearLightingSettings.glowmapMult : color;
	}

	float3 Ambient(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.ambientGamma) * SharedData::linearLightingSettings.ambientMult : color;
	}

	float3 Fog(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.fogGamma) : color;
	}

	float FogAlpha(float alpha)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(alpha), SharedData::linearLightingSettings.fogAlphaGamma) : alpha;
	}

	float3 Effect(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.effectGamma) : color;
	}

	float3 EffectMult(float3 color)
	{
		if (ENABLE_BRIGHTNESS_ADJUSTMENTS) {
#	if defined(MEMBRANE)
			color *= SharedData::linearLightingSettings.membraneEffectMult;
#	elif defined(BLOOD)
			color *= SharedData::linearLightingSettings.bloodEffectMult;
#	elif defined(PROJECTED_UV)
			color *= SharedData::linearLightingSettings.projectedEffectMult;
#	elif defined(DEFERRED)
			color *= SharedData::linearLightingSettings.deferredEffectMult;
#	else
			color *= SharedData::linearLightingSettings.otherEffectMult;
#	endif
		}
		return color;
	}

	float EffectLightingMult()
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? SharedData::linearLightingSettings.effectLightingMult : 1.0f;
	}

	float EffectAlpha(float alpha)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(alpha), SharedData::linearLightingSettings.effectAlphaGamma) : alpha;
	}

	float3 Sky(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.skyGamma) : color;
	}

	float3 Water(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.waterGamma) : color;
	}

	float3 VolumetricLighting(float3 color)
	{
		return ENABLE_BRIGHTNESS_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.vlGamma) : color;
	}

	float3 ColorToLinear(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? pow(abs(color), SharedData::linearLightingSettings.colorGamma) : color;
	}

	float3 RadianceToLinear(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : SkyrimGammaToLinear(color);
	}

	float IrradianceToLinear(float color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : SkyrimGammaToLinear(color);
	}

	float IrradianceToGamma(float color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : LinearToSkyrimGamma(color);
	}

	float3 IrradianceToLinear(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : SkyrimGammaToLinear(color);
	}

	float3 IrradianceToGamma(float3 color)
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? color : LinearToSkyrimGamma(color);
	}

	float VanillaNormalization()
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? 1.0 / Math::PI : 1.0f;
	}

	float VanillaDiffuseColorMult()
	{
		return ENABLE_LL_COLOR_ADJUSTMENTS ? SharedData::linearLightingSettings.vanillaDiffuseColorMult : 1.0f;
	}
#else
	const static float PBRLightingScale = 1.0;
	const static float ReflectionNormalisationScale = 1.0;
	const static float PBRLightingCompensation = Math::PI;

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return LinearToSrgb(color);
#	else
		return color;
#	endif
	}

	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR)
		return color * Math::PI;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}
#endif
}

#endif  //__COLOR_DEPENDENCY_HLSL__
