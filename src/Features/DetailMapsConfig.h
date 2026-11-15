#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace DetailMapsConfig
{
	struct Settings
	{
		bool Enabled = true;
		float Tiling = 1.0f;
		float AlbedoStrength = 1.0f;
		float NormalStrength = 1.0f;
		float RoughnessStrength = 1.0f;
		float FadeStart = 400.0f;
		float FadeEnd = 1200.0f;
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, Enabled, Tiling, AlbedoStrength,
		NormalStrength, RoughnessStrength, FadeStart, FadeEnd)

	struct Number
	{
		const char* name;
		const char* label;
		float Settings::* member;
		double minimum, maximum;
	};

	inline constexpr auto Numbers = std::to_array<Number>({ { "Tiling", "Tiling", &Settings::Tiling, 0.1, 8.0 },
		{ "AlbedoStrength", "Color Strength", &Settings::AlbedoStrength, 0, 4 },
		{ "NormalStrength", "Bump Strength", &Settings::NormalStrength, 0, 4 },
		{ "RoughnessStrength", "Roughness Strength", &Settings::RoughnessStrength, 0, 4 },
		{ "FadeStart", "Fade Start", &Settings::FadeStart, 0, 8000 },
		{ "FadeEnd", "Fade End", &Settings::FadeEnd, 1, 8001 } });

	/** @brief Saved settings recover finite defaults before any render-thread use. */
	inline void Clamp(Settings& settings)
	{
		const Settings defaults;
		for (const auto& field : Numbers) {
			auto& value = settings.*field.member;
			value = std::clamp(std::isfinite(value) ? value : defaults.*field.member, static_cast<float>(field.minimum), static_cast<float>(field.maximum));
		}
		settings.FadeEnd = std::max(settings.FadeEnd, settings.FadeStart + 1.0f);
	}

	/** @brief Edits are atomic and reject unknown keys, invalid types and crossed fade bounds. */
	inline Settings Patch(const Settings& original, const nlohmann::json& edit)
	{
		if (!edit.is_object())
			throw std::invalid_argument("detailMaps must be an object");
		auto next = original;
		for (const auto& [key, value] : edit.items()) {
			if (key == "Enabled") {
				if (!value.is_boolean())
					throw std::invalid_argument("Enabled must be boolean");
				next.Enabled = value.get<bool>();
				continue;
			}
			const auto field = std::ranges::find(Numbers, std::string_view(key), &Number::name);
			if (field == Numbers.end())
				throw std::invalid_argument("Unknown Detail Maps setting: " + key);
			if (!value.is_number())
				throw std::invalid_argument(key + " must be numeric");
			const double number = value.get<double>();
			if (!std::isfinite(number) || number < field->minimum || number > field->maximum)
				throw std::invalid_argument(key + " is outside its supported range");
			next.*field->member = static_cast<float>(number);
		}
		if (next.FadeEnd < next.FadeStart + 1.0f)
			throw std::invalid_argument("FadeEnd must be at least FadeStart + 1");
		return next;
	}

	/** @brief The menu, saved-setting clamps and DevBench schema share the same ranges. */
	inline nlohmann::json Properties()
	{
		nlohmann::json properties{ { "Enabled", { { "type", "boolean" } } } };
		for (const auto& field : Numbers)
			properties[field.name] = { { "type", "number" }, { "minimum", field.minimum }, { "maximum", field.maximum } };
		return properties;
	}
}
