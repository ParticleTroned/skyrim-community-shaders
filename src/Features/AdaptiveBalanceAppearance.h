#pragma once

#include "Utils/Finite.h"
#include "VolumetricLightingTuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

/** Neutral appearance adjustments shared by profile composition and shader upload. */
struct AdaptiveBalanceAppearanceSettings
{
	float directionalSaturation = 1.0f;
	float directionalCurve = 1.0f;
	float ambientSaturation = 1.0f;
	float fogBrightness = 1.0f;
	float3 directionalTint = { 1.0f, 1.0f, 1.0f };
	float cloudOpacity = 1.0f;
	float3 fogTint = { 1.0f, 1.0f, 1.0f };
	float starsIntensity = 1.0f;
	float3 cloudTint = { 1.0f, 1.0f, 1.0f };
	float starsCurve = 1.0f;
	float3 skyTopTint = { 1.0f, 1.0f, 1.0f };
	float skyTopIntensity = 1.0f;
	float3 skyMiddleTint = { 1.0f, 1.0f, 1.0f };
	float skyMiddleIntensity = 1.0f;
	float3 skyHorizonTint = { 1.0f, 1.0f, 1.0f };
	float skyHorizonIntensity = 1.0f;
	float skyTopCurve = 1.0f;
	float skyMiddleCurve = 1.0f;
	float skyHorizonCurve = 1.0f;
	float skyStaticCurve = 1.0f;
	float3 skyStaticTint = { 1.0f, 1.0f, 1.0f };
	float lightSpriteIntensity = 1.0f;
	float lightSpriteCurve = 1.0f;
	float particleIntensity = 1.0f;
	float particleDirectionalInfluence = 1.0f;
	float particleAmbientInfluence = 1.0f;
	float particlePointInfluence = 1.0f;
	float cloudShadowStrength = 1.0f;
	float godrayIntensity = 1.0f;
	float godrayOpacity = 1.0f;
	float3 godrayTint = { 1.0f, 1.0f, 1.0f };
	float godrayTintAmount = 0.0f;
	float godraySaturation = 1.0f;
	float eyeSpecularBrightness = 1.0f;
	float eyeBrightness = 1.0f;
	float auroraIntensity = 1.0f;
	float auroraCurve = 1.0f;
	float3 padding{};
	float sceneGammaOffset = 0.0f;
	float ambientGammaOffset = 0.0f;
	float directionalGammaOffset = 0.0f;
	float pointLightGammaOffset = 0.0f;
	float linearPointLightSaturation = 1.0f;
	float linearPointLightGammaOffset = 0.0f;
	float spotlightSaturation = 1.0f;
	float spotlightGammaOffset = 0.0f;
	float linearSpotlightSaturation = 1.0f;
	float linearSpotlightGammaOffset = 0.0f;
	float omnidirectionalBulbSaturation = 1.0f;
	float omnidirectionalBulbGammaOffset = 0.0f;
	float linearOmnidirectionalBulbSaturation = 1.0f;
	float linearOmnidirectionalBulbGammaOffset = 0.0f;
	float emissiveSaturation = 1.0f;
	float emissiveGammaOffset = 0.0f;
	float glowmapSaturation = 1.0f;
	float glowmapGammaOffset = 0.0f;
	float effectLightingSaturation = 1.0f;
	float effectLightingGammaOffset = 0.0f;
	float effectSaturation = 1.0f;
	float effectGammaOffset = 0.0f;
	float skyStaticSaturation = 1.0f;
	float skyStaticGammaOffset = 0.0f;
	float sunGlareSaturation = 1.0f;
	float sunGlareGammaOffset = 0.0f;
	float fogSaturation = 1.0f;
	float volumetricIntensity = 1.0f;
	float volumetricSaturation = 1.0f;
	float nonlinearPointLightSaturation = 1.0f;
	uint pointLightColorEnabled = 0;  // Derived on upload to bypass subtype work for neutral profiles.
	float utilityPadding = 0.0f;
};
static_assert(sizeof(AdaptiveBalanceAppearanceSettings) == 352);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, sceneGammaOffset) == 224);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, volumetricSaturation) == 336);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, godraySaturation) == 192);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, eyeSpecularBrightness) == 196);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, eyeBrightness) == 200);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, auroraIntensity) == 204);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, auroraCurve) == 208);

namespace AdaptiveBalanceAppearance
{
	using Settings = AdaptiveBalanceAppearanceSettings;
	struct ScalarField
	{
		const char* name;
		const char* label;
		const char* group;
		float Settings::* member;
		float minimum;
		float maximum;
		float neutral;
	};
	struct TintField
	{
		const char* name;
		const char* label;
		const char* group;
		float3 Settings::* member;
		float maximum;
	};
	inline constexpr std::array kScalars{
		ScalarField{ "sceneGammaOffset", "Scene Gamma Offset", "Scene Colour", &Settings::sceneGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "ambientGammaOffset", "Ambient Gamma Offset", "Direct and Ambient Lighting", &Settings::ambientGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "directionalGammaOffset", "Directional Gamma Offset", "Direct and Ambient Lighting", &Settings::directionalGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "pointLightGammaOffset", "Nonlinear Point Lights Gamma Offset", "Point Light Colour", &Settings::pointLightGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "nonlinearPointLightSaturation", "Nonlinear Point Lights Saturation", "Point Light Colour", &Settings::nonlinearPointLightSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "linearPointLightSaturation", "Linear Point Lights Saturation", "Point Light Colour", &Settings::linearPointLightSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "linearPointLightGammaOffset", "Linear Point Lights Gamma Offset", "Point Light Colour", &Settings::linearPointLightGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "spotlightSaturation", "Spotlights Saturation", "Point Light Colour", &Settings::spotlightSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "spotlightGammaOffset", "Spotlights Gamma Offset", "Point Light Colour", &Settings::spotlightGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "linearSpotlightSaturation", "Linear Spotlights Saturation", "Point Light Colour", &Settings::linearSpotlightSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "linearSpotlightGammaOffset", "Linear Spotlights Gamma Offset", "Point Light Colour", &Settings::linearSpotlightGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "omnidirectionalBulbSaturation", "Omnidirectional Bulbs Saturation", "Point Light Colour", &Settings::omnidirectionalBulbSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "omnidirectionalBulbGammaOffset", "Omnidirectional Bulbs Gamma Offset", "Point Light Colour", &Settings::omnidirectionalBulbGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "linearOmnidirectionalBulbSaturation", "Linear Omnidirectional Bulbs Saturation", "Point Light Colour", &Settings::linearOmnidirectionalBulbSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "linearOmnidirectionalBulbGammaOffset", "Linear Omnidirectional Bulbs Gamma Offset", "Point Light Colour", &Settings::linearOmnidirectionalBulbGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "emissiveSaturation", "Emissive Saturation", "Material Colour", &Settings::emissiveSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "emissiveGammaOffset", "Emissive Gamma Offset", "Material Colour", &Settings::emissiveGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "glowmapSaturation", "Glowmaps Saturation", "Material Colour", &Settings::glowmapSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "glowmapGammaOffset", "Glowmaps Gamma Offset", "Material Colour", &Settings::glowmapGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "effectLightingSaturation", "Effect Lighting Saturation", "Material Colour", &Settings::effectLightingSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "effectLightingGammaOffset", "Effect Lighting Gamma Offset", "Material Colour", &Settings::effectLightingGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "effectSaturation", "Weather-Lit Effects Saturation", "Atmosphere Colour", &Settings::effectSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "effectGammaOffset", "Weather-Lit Effects Gamma Offset", "Atmosphere Colour", &Settings::effectGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "skyStaticSaturation", "Sky Statics Saturation", "Atmosphere Colour", &Settings::skyStaticSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "skyStaticGammaOffset", "Sky Statics Gamma Offset", "Atmosphere Colour", &Settings::skyStaticGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "sunGlareSaturation", "Sun Glare Saturation", "Atmosphere Colour", &Settings::sunGlareSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "sunGlareGammaOffset", "Sun Glare Gamma Offset", "Atmosphere Colour", &Settings::sunGlareGammaOffset, -1.0f, 1.0f, 0.0f },
		ScalarField{ "fogSaturation", "Vanilla Fog Saturation", "Fog and Clouds", &Settings::fogSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "volumetricIntensity", "Volumetric Lighting Intensity", "Godrays and Cloud Shadows", &Settings::volumetricIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "volumetricSaturation", "Volumetric Lighting Saturation", "Godrays and Cloud Shadows", &Settings::volumetricSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "eyeSpecularBrightness", "Eye Specular Brightness", "Eyes", &Settings::eyeSpecularBrightness, 0.0f, 5.0f, 1.0f },
		ScalarField{ "eyeBrightness", "Eye Brightness", "Eyes", &Settings::eyeBrightness, 0.0f, 5.0f, 1.0f },
		ScalarField{ "directionalSaturation", "Directional Saturation", "Direct and Ambient Lighting", &Settings::directionalSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "directionalCurve", "Directional Colour Curve", "Direct and Ambient Lighting", &Settings::directionalCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "ambientSaturation", "Ambient Saturation", "Direct and Ambient Lighting", &Settings::ambientSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "fogBrightness", "Fog Colour Brightness", "Fog and Clouds", &Settings::fogBrightness, 0.0f, 5.0f, 1.0f },
		ScalarField{ "cloudOpacity", "Cloud Opacity", "Fog and Clouds", &Settings::cloudOpacity, 0.0f, 2.0f, 1.0f },
		ScalarField{ "starsIntensity", "Stars Intensity", "Stars", &Settings::starsIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "starsCurve", "Stars Colour Curve", "Stars", &Settings::starsCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "auroraIntensity", "Aurora Intensity", "Auroras", &Settings::auroraIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "auroraCurve", "Aurora Colour Curve", "Auroras", &Settings::auroraCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "skyTopIntensity", "Upper Sky Intensity", "Sky Gradient", &Settings::skyTopIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "skyMiddleIntensity", "Middle Sky Intensity", "Sky Gradient", &Settings::skyMiddleIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "skyHorizonIntensity", "Horizon Intensity", "Sky Gradient", &Settings::skyHorizonIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "skyTopCurve", "Upper Sky Curve", "Sky Gradient", &Settings::skyTopCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "skyMiddleCurve", "Middle Sky Curve", "Sky Gradient", &Settings::skyMiddleCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "skyHorizonCurve", "Horizon Curve", "Sky Gradient", &Settings::skyHorizonCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "skyStaticCurve", "Mist Colour Curve", "Sky Static Mist", &Settings::skyStaticCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "lightSpriteIntensity", "Glow Sprite Intensity", "Particles and Glow Sprites", &Settings::lightSpriteIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "lightSpriteCurve", "Glow Sprite Curve", "Particles and Glow Sprites", &Settings::lightSpriteCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "particleIntensity", "Particle Intensity", "Particles and Glow Sprites", &Settings::particleIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "particleDirectionalInfluence", "Particle Directional Influence", "Particles and Glow Sprites", &Settings::particleDirectionalInfluence, 0.0f, 5.0f, 1.0f },
		ScalarField{ "particleAmbientInfluence", "Particle Ambient Influence", "Particles and Glow Sprites", &Settings::particleAmbientInfluence, 0.0f, 5.0f, 1.0f },
		ScalarField{ "particlePointInfluence", "Particle Point Light Influence", "Particles and Glow Sprites", &Settings::particlePointInfluence, 0.0f, 5.0f, 1.0f },
		ScalarField{ "cloudShadowStrength", "Cloud Shadow Strength", "Godrays and Cloud Shadows", &Settings::cloudShadowStrength, 0.0f, 2.0f, 1.0f },
		ScalarField{ "godrayIntensity", "Godray Intensity Scale", "Godrays and Cloud Shadows", &Settings::godrayIntensity, 0.0f, VolumetricLightingTuning::kShaftIntensityMax, 1.0f },
		ScalarField{ "godrayOpacity", "Godray Opacity Scale", "Godrays and Cloud Shadows", &Settings::godrayOpacity, 0.0f, VolumetricLightingTuning::kOpacityMax, 1.0f },
		ScalarField{ "godrayTintAmount", "Godray Custom Colour Contribution", "Godrays and Cloud Shadows", &Settings::godrayTintAmount, 0.0f, 1.0f, 0.0f },
		ScalarField{ "godraySaturation", "Godray Saturation Scale", "Godrays and Cloud Shadows", &Settings::godraySaturation, 0.0f, VolumetricLightingTuning::kSaturationMax, 1.0f }
	};
	inline constexpr std::array kTints{
		TintField{ "directionalTint", "Directional Tint", "Direct and Ambient Lighting", &Settings::directionalTint, 2.0f },
		TintField{ "fogTint", "Fog Tint", "Fog and Clouds", &Settings::fogTint, 2.0f },
		TintField{ "cloudTint", "Cloud Tint", "Fog and Clouds", &Settings::cloudTint, 2.0f },
		TintField{ "skyTopTint", "Upper Sky Tint", "Sky Gradient", &Settings::skyTopTint, 2.0f },
		TintField{ "skyMiddleTint", "Middle Sky Tint", "Sky Gradient", &Settings::skyMiddleTint, 2.0f },
		TintField{ "skyHorizonTint", "Horizon Tint", "Sky Gradient", &Settings::skyHorizonTint, 2.0f },
		TintField{ "skyStaticTint", "Mist Tint", "Sky Static Mist", &Settings::skyStaticTint, 2.0f },
		TintField{ "godrayTint", "Godray Custom Colour", "Godrays and Cloud Shadows", &Settings::godrayTint, 1.0f }
	};

	/** Requests vanilla light flags when any authored subtype has a colour adjustment. */
	inline bool UsesClassifiedPointLights(const Settings& settings)
	{
		return settings.nonlinearPointLightSaturation != 1.0f || settings.pointLightGammaOffset != 0.0f ||
		       settings.linearPointLightSaturation != 1.0f ||
		       settings.linearPointLightGammaOffset != 0.0f ||
		       settings.spotlightSaturation != 1.0f ||
		       settings.spotlightGammaOffset != 0.0f ||
		       settings.linearSpotlightSaturation != 1.0f ||
		       settings.linearSpotlightGammaOffset != 0.0f ||
		       settings.omnidirectionalBulbSaturation != 1.0f ||
		       settings.omnidirectionalBulbGammaOffset != 0.0f ||
		       settings.linearOmnidirectionalBulbSaturation != 1.0f ||
		       settings.linearOmnidirectionalBulbGammaOffset != 0.0f;
	}

	/** Bounds user input before it reaches profile arithmetic or rendering. */
	inline void Sanitize(Settings& settings)
	{
		for (const auto& field : kScalars)
			settings.*field.member = Util::ClampFinite(settings.*field.member, field.minimum, field.maximum, field.neutral);
		for (const auto& field : kTints) {
			auto& tint = settings.*field.member;
			tint.x = Util::ClampFinite(tint.x, 0.0f, field.maximum, 1.0f);
			tint.y = Util::ClampFinite(tint.y, 0.0f, field.maximum, 1.0f);
			tint.z = Util::ClampFinite(tint.z, 0.0f, field.maximum, 1.0f);
		}
	}

	/** Composes bounded multipliers and overlays custom godray colours without changing the base. */
	inline Settings Compose(Settings base, Settings layer)
	{
		Sanitize(base);
		Sanitize(layer);
		const auto godrayColor = VolumetricLightingTuning::ComposeProfiles(
			{ .CustomColorContribution = base.godrayTintAmount, .CustomColorRed = base.godrayTint.x, .CustomColorGreen = base.godrayTint.y, .CustomColorBlue = base.godrayTint.z },
			{ .CustomColorContribution = layer.godrayTintAmount, .CustomColorRed = layer.godrayTint.x, .CustomColorGreen = layer.godrayTint.y, .CustomColorBlue = layer.godrayTint.z });
		for (const auto& field : kScalars) {
			if (field.neutral == 0.0f)
				base.*field.member += layer.*field.member;
			else
				base.*field.member *= layer.*field.member;
		}
		for (const auto& field : kTints) {
			auto& output = base.*field.member;
			const auto& input = layer.*field.member;
			output.x *= input.x;
			output.y *= input.y;
			output.z *= input.z;
		}
		base.godrayTintAmount = godrayColor.CustomColorContribution;
		base.godrayTint = { godrayColor.CustomColorRed, godrayColor.CustomColorGreen, godrayColor.CustomColorBlue };
		Sanitize(base);
		return base;
	}

	/** Interpolates composed branches; premultiplied tint prevents colours from inactive layers bleeding in. */
	inline Settings Lerp(Settings from, Settings to, float factor)
	{
		Sanitize(from);
		Sanitize(to);
		factor = Util::ClampFinite(factor, 0.0f, 1.0f, 0.0f);
		if (factor == 0.0f)
			return from;
		if (factor == 1.0f)
			return to;
		auto output = from;
		for (const auto& field : kScalars)
			output.*field.member = std::lerp(from.*field.member, to.*field.member, factor);
		for (const auto& field : kTints) {
			auto& value = output.*field.member;
			const auto& a = from.*field.member;
			const auto& b = to.*field.member;
			value = { std::lerp(a.x, b.x, factor), std::lerp(a.y, b.y, factor), std::lerp(a.z, b.z, factor) };
		}
		if (output.godrayTintAmount > 0.0f) {
			const float a = from.godrayTintAmount * (1.0f - factor) / output.godrayTintAmount;
			const float b = to.godrayTintAmount * factor / output.godrayTintAmount;
			output.godrayTint = { from.godrayTint.x * a + to.godrayTint.x * b,
				from.godrayTint.y * a + to.godrayTint.y * b,
				from.godrayTint.z * a + to.godrayTint.z * b };
		}
		return output;
	}
}

inline void to_json(nlohmann::json& json, const AdaptiveBalanceAppearanceSettings& value)
{
	json = nlohmann::json::object();
	for (const auto& field : AdaptiveBalanceAppearance::kScalars)
		json[field.name] = value.*field.member;
	for (const auto& field : AdaptiveBalanceAppearance::kTints) {
		const auto& tint = value.*field.member;
		json[field.name] = { tint.x, tint.y, tint.z };
	}
}

inline void from_json(const nlohmann::json& json, AdaptiveBalanceAppearanceSettings& value)
{
	if (!json.is_object())
		throw nlohmann::json::type_error::create(302, "Adaptive Balance appearance must be an object", &json);
	AdaptiveBalanceAppearanceSettings parsed;
	for (const auto& field : AdaptiveBalanceAppearance::kScalars) {
		const auto input = json.find(field.name);
		if (input == json.end())
			continue;
		if (!input->is_number())
			throw nlohmann::json::type_error::create(302, std::string(field.name) + " must be numeric", &json);
		parsed.*field.member = input->get<float>();
	}
	for (const auto& field : AdaptiveBalanceAppearance::kTints) {
		const auto input = json.find(field.name);
		if (input == json.end())
			continue;
		if (!input->is_array() || input->size() != 3 ||
			!std::all_of(input->begin(), input->end(), [](const auto& channel) { return channel.is_number(); }))
			throw nlohmann::json::type_error::create(302, std::string(field.name) + " must have exactly three numeric channels", &json);
		const auto tint = input->get<std::array<float, 3>>();
		parsed.*field.member = { tint[0], tint[1], tint[2] };
	}
	AdaptiveBalanceAppearance::Sanitize(parsed);
	value = parsed;
}
