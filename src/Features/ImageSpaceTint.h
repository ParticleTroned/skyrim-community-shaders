#pragma once

#include "Utils/Finite.h"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <string>

namespace ImageSpaceTint
{
	inline constexpr float kMinimum = 0.0f;
	inline constexpr float kMaximum = 1.0f;
	inline constexpr float kNativeAmount = -1.0f;

	/** Optional replacement for the native image-space TNAM colour and amount. */
	struct Settings
	{
		bool enabled = false;
		float amount = 0.5f;
		std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
	};

	/** Keep saved and uploaded tint parameters finite and within native bounds. */
	inline void Sanitize(Settings& settings)
	{
		const Settings defaults;
		settings.amount = Util::ClampFinite(settings.amount, kMinimum, kMaximum, defaults.amount);
		for (auto& channel : settings.color)
			channel = Util::ClampFinite(channel, kMinimum, kMaximum, defaults.color[0]);
	}

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, enabled, amount, color)

	/** Describe partial updates with the same bounds and defaults as the saved controls. */
	inline nlohmann::json Schema()
	{
		const Settings defaults;
		const nlohmann::json number{ { "type", "number" }, { "minimum", kMinimum }, { "maximum", kMaximum } };
		auto amount = number;
		amount["default"] = defaults.amount;
		return {
			{ "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false },
			{ "description", "Partial Global image-space tint override. Disabled by default; omitted fields retain their values. Independent of lightingAdvanced. Visuals off restores native tint." },
			{ "properties", { { "enabled", { { "type", "boolean" }, { "default", defaults.enabled } } },
								{ "amount", amount },
								{ "color", { { "type", "array" }, { "minItems", defaults.color.size() }, { "maxItems", defaults.color.size() }, { "items", number }, { "default", defaults.color } } } } }
		};
	}

	/** Reject malformed or out-of-range updates before changing any field. */
	inline std::string ValidateUpdate(const nlohmann::json& update)
	{
		if (!update.is_object() || update.empty())
			return "imageSpaceTint must be a nonempty object";
		const auto validNumber = [](const nlohmann::json& channel) {
			if (!channel.is_number())
				return false;
			const auto number = channel.get<double>();
			return Util::IsFinite(number) && number >= kMinimum && number <= kMaximum;
		};
		for (const auto& [key, value] : update.items()) {
			if (key == "enabled" && value.is_boolean())
				continue;
			if (key == "amount" && validNumber(value))
				continue;
			if (key == "color" && value.is_array() && value.size() == Settings{}.color.size() && std::ranges::all_of(value, validNumber))
				continue;
			return "Unknown or invalid image-space tint field: " + key;
		}
		return {};
	}

	/** Apply a validated patch atomically, preserving omitted controls. */
	inline std::string ApplyUpdate(const nlohmann::json& update, Settings& settings)
	{
		if (auto error = ValidateUpdate(update); !error.empty())
			return error;
		auto candidate = settings;
		candidate.enabled = update.value("enabled", candidate.enabled);
		candidate.amount = update.value("amount", candidate.amount);
		candidate.color = update.value("color", candidate.color);
		settings = candidate;
		return {};
	}
}
