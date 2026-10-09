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
	float padding[3]{};
};
static_assert(sizeof(AdaptiveBalanceAppearanceSettings) == 208);
static_assert(offsetof(AdaptiveBalanceAppearanceSettings, godraySaturation) == 192);

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
		ScalarField{ "directionalSaturation", "Directional Saturation", "Direct and Ambient Lighting", &Settings::directionalSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "directionalCurve", "Directional Colour Curve", "Direct and Ambient Lighting", &Settings::directionalCurve, 0.1f, 4.0f, 1.0f },
		ScalarField{ "ambientSaturation", "Ambient Saturation", "Direct and Ambient Lighting", &Settings::ambientSaturation, 0.0f, 2.0f, 1.0f },
		ScalarField{ "fogBrightness", "Fog Colour Brightness", "Fog and Clouds", &Settings::fogBrightness, 0.0f, 5.0f, 1.0f },
		ScalarField{ "cloudOpacity", "Cloud Opacity", "Fog and Clouds", &Settings::cloudOpacity, 0.0f, 2.0f, 1.0f },
		ScalarField{ "starsIntensity", "Stars Intensity", "Stars", &Settings::starsIntensity, 0.0f, 5.0f, 1.0f },
		ScalarField{ "starsCurve", "Stars Colour Curve", "Stars", &Settings::starsCurve, 0.1f, 4.0f, 1.0f },
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
		for (const auto& field : kScalars)
			base.*field.member *= layer.*field.member;
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
