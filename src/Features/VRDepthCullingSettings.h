#pragma once

#include "VRDepthCullingEnablePolicy.h"

#include <cmath>
#include <nlohmann/json.hpp>

namespace VRDepthCullingSettings
{
	/** Migrates shared thresholds and validates saved location settings before deserialization. */
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
		if (!hasLocationExtents && hadMasterSwitch && !exteriorEnabled)
			interiorEnabled = false;

		const float sharedExtent = readExtent("MinOccludeeBoxExtent", VRDepthCullingEnablePolicy::kDefaultMinimumExtent);
		const float exteriorExtent = readExtent("MinOccludeeBoxExtentExterior", sharedExtent);
		const float interiorExtent = readExtent("MinOccludeeBoxExtentInterior", sharedExtent);
		a_settings["EnableDepthBufferCullingExterior"] = exteriorEnabled;
		a_settings["EnableDepthBufferCullingInterior"] = interiorEnabled;
		a_settings["DepthCullingLegacyMode"] = readBoolean("DepthCullingLegacyMode", false);
		a_settings["MinOccludeeBoxExtentExterior"] = exteriorExtent;
		a_settings["MinOccludeeBoxExtentInterior"] = interiorExtent;
		a_settings.erase("MinOccludeeBoxExtent");
		return malformed;
	}
}
