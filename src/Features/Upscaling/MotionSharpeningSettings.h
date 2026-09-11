#pragma once

#include "MotionSharpeningPolicy.h"

#include <nlohmann/json.hpp>

namespace MotionSharpening
{
	/** Repairs only optional motion fields before the enclosing settings deserializer runs. */
	inline bool NormalizeLoadedSettings(nlohmann::json& settings)
	{
		if (!settings.is_object())
			return false;
		bool malformed = false;
		const Settings defaults{};
		const auto readNumber = [&](const char* key, double fallback) {
			const auto it = settings.find(key);
			if (it == settings.end())
				return fallback;
			if (!it->is_number()) {
				malformed = true;
				return fallback;
			}
			const double value = it->get<double>();
			if (!std::isfinite(value)) {
				malformed = true;
				return fallback;
			}
			return value;
		};
		bool enabled = defaults.enabled;
		if (const auto it = settings.find("motionAdaptiveRCAS"); it != settings.end()) {
			if (it->is_boolean())
				enabled = it->get<bool>();
			else
				malformed = true;
		}
		const auto bounded = Sanitize(enabled,
			readNumber("motionSharpnessAdjustment", defaults.adjustment),
			readNumber("motionSharpnessThreshold", defaults.thresholdPixels),
			readNumber("motionSharpnessCap", defaults.strengthCap));
		settings["motionAdaptiveRCAS"] = bounded.enabled;
		settings["motionSharpnessAdjustment"] = bounded.adjustment;
		settings["motionSharpnessThreshold"] = bounded.thresholdPixels;
		settings["motionSharpnessCap"] = bounded.strengthCap;
		return malformed;
	}
}
