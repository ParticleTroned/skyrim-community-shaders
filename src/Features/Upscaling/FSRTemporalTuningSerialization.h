#pragma once

#include "FSRTemporalTuningPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace FSRTemporalTuningPolicy
{
	/** Validates the complete patch before publishing; persisted unknown fields remain forward compatible. */
	[[nodiscard]] inline const char* ApplySettingsPatch(const nlohmann::json& a_patch, Settings& a_settings, bool a_rejectUnknown = true)
	{
		if (!a_patch.is_object())
			return "tuning settings must be an object";
		auto candidate = a_settings;
		for (const auto& [key, value] : a_patch.items()) {
			if (key == "enabled") {
				if (!value.is_boolean())
					return "enabled must be boolean";
				candidate.enabled = value.get<bool>();
				continue;
			}
			const auto field = std::ranges::find_if(kNumericSettings, [&](const auto& item) { return key == item.name; });
			if (field == kNumericSettings.end()) {
				if (a_rejectUnknown)
					return "unknown tuning setting";
				continue;
			}
			if (!value.is_number())
				return "tuning values must be numeric";
			const double number = value.get<double>();
			if (!std::isfinite(number) || number < field->minimum || number > field->maximum)
				return "tuning values must be finite and within the declared ranges";
			candidate.*field->member = static_cast<float>(number);
		}
		if (!IsValid(candidate))
			return "tuning values must be finite and within the declared ranges";
		a_settings = candidate;
		return nullptr;
	}

	/** Persists every supported field from the same definition used for validation. */
	inline void to_json(nlohmann::json& a_json, const Settings& a_settings)
	{
		a_json = { { "enabled", a_settings.enabled } };
		for (const auto& field : kNumericSettings)
			a_json[field.name] = a_settings.*field.member;
	}
}
