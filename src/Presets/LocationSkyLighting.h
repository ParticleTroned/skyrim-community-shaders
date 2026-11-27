#pragma once

#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

namespace Presets::LocationSkyLighting
{
	inline constexpr float MaxShadowElevation = 45.0f;
	inline constexpr float MaxHorizonFadeHours = 1.5f;

	/** Empty objects inherit both fields; supplied values are absolute overrides. */
	inline bool Validate(const nlohmann::json& values, std::string& error)
	{
		if (!values.is_object() || values.size() > 2) {
			error = "Sky lighting must be an object containing at most two settings";
			return false;
		}
		for (const auto& [key, value] : values.items()) {
			const float maximum = key == "MinShadowElevation" ? MaxShadowElevation :
			                      key == "HorizonFadeHours"   ? MaxHorizonFadeHours :
			                                                    -1.0f;
			if (maximum < 0 || !value.is_number() || !std::isfinite(value.get<double>()) ||
				value.get<double>() < 0 || value.get<double>() > maximum) {
				error = "Invalid sky lighting setting: " + key;
				return false;
			}
		}
		return true;
	}
}
