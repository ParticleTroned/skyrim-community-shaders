#pragma once

#include "VRDepthCullingEnablePolicy.h"
#include "VRDepthCullingTemporal.h"

#include <cmath>
#include <nlohmann/json.hpp>

namespace VRDepthCullingSettings
{
	/** Migrate thresholds and method preferences before deserializing saved settings. */
	inline bool NormalizeLoadedSettings(nlohmann::json& a_settings)
	{
		if (!a_settings.is_object())
			return false;

		bool malformed = false;
		const auto readBoolean = [&](const char* a_key, bool a_default) {
			const auto it = a_settings.find(a_key);
			if (it == a_settings.end())
				return a_default;
			if (!it->is_boolean()) {
				malformed = true;
				return a_default;
			}
			return it->get<bool>();
		};
		const auto readExtent = [&](const char* a_key, float a_default) {
			const auto it = a_settings.find(a_key);
			if (it == a_settings.end())
				return a_default;
			if (!it->is_number()) {
				malformed = true;
				return a_default;
			}
			const double value = it->get<double>();
			const float bounded = VRDepthCullingEnablePolicy::SanitizeMinimumExtent(value);
			malformed |= !std::isfinite(value) ||
			             value < VRDepthCullingEnablePolicy::kMinimumExtent ||
			             value > VRDepthCullingEnablePolicy::kMaximumExtent;
			return bounded;
		};

		const bool hasLocationExtents = a_settings.contains("MinOccludeeBoxExtentExterior") ||
		                                a_settings.contains("MinOccludeeBoxExtentInterior");
		const bool exteriorEnabled = readBoolean("EnableDepthBufferCullingExterior", true);
		bool interiorEnabled = readBoolean("EnableDepthBufferCullingInterior", true);
		const bool hadMasterSwitch = a_settings.contains("DepthCullingLegacyMode") ||
		                             a_settings.contains("DepthCullingPerformanceMode");
		// Pre-policy configurations already had independent location switches.
		if (!hasLocationExtents && hadMasterSwitch && !a_settings.contains("DepthCullingMethod") && !exteriorEnabled)
			interiorEnabled = false;

		using VRDepthCullingTemporal::Mode;
		auto mode = VRDepthCullingTemporal::SelectMode(readBoolean("DepthCullingLegacyMode", false));
		if (const auto method = a_settings.find("DepthCullingMethod"); method != a_settings.end()) {
			mode = Mode::Balanced;
			if (method->is_number_integer() &&
				(*method == static_cast<int>(Mode::Balanced) || *method == static_cast<int>(Mode::Legacy) ||
					*method == static_cast<int>(Mode::Hybrid))) {
				mode = static_cast<Mode>(method->get<int>());
			} else {
				malformed = true;
			}
		}

		const float sharedExtent = readExtent("MinOccludeeBoxExtent", VRDepthCullingEnablePolicy::kDefaultMinimumExtent);
		const float exteriorExtent = readExtent("MinOccludeeBoxExtentExterior", sharedExtent);
		const float interiorExtent = readExtent("MinOccludeeBoxExtentInterior", sharedExtent);
		a_settings["EnableDepthBufferCullingExterior"] = exteriorEnabled;
		a_settings["EnableDepthBufferCullingInterior"] = interiorEnabled;
		a_settings["DepthCullingMethod"] = static_cast<int>(mode);
		a_settings["DepthCullingLegacyMode"] = mode == Mode::Legacy;
		a_settings["MinOccludeeBoxExtentExterior"] = exteriorExtent;
		a_settings["MinOccludeeBoxExtentInterior"] = interiorExtent;
		a_settings.erase("MinOccludeeBoxExtent");
		return malformed;
	}
}
