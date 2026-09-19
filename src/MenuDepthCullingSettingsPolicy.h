#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/VRDepthCullingEnablePolicy.h"

#	include <nlohmann/json.hpp>

#	include <cmath>
#	include <optional>
#	include <string>

namespace MenuDepthCullingSettingsPolicy
{
	struct Update
	{
		std::optional<bool> exteriorEnabled;
		std::optional<bool> interiorEnabled;
		std::optional<float> exteriorMinExtent;
		std::optional<float> interiorMinExtent;
	};

	/** Validate the complete partial update before publishing any staged values. */
	inline bool TryParse(const nlohmann::json& a_value, Update& a_update, std::string& a_error)
	{
		if (!a_value.is_object() || a_value.empty()) {
			a_error = "depthCulling must be a nonempty object";
			return false;
		}
		Update update;
		for (const auto& [name, value] : a_value.items()) {
			if (name == "exteriorEnabled" || name == "interiorEnabled") {
				if (!value.is_boolean()) {
					a_error = "Depth-culling enable field must be boolean: " + name;
					return false;
				}
				(name == "exteriorEnabled" ? update.exteriorEnabled : update.interiorEnabled) = value.get<bool>();
			} else if (name == "exteriorMinExtent" || name == "interiorMinExtent") {
				if (!value.is_number()) {
					a_error = "Depth-culling extent must be numeric: " + name;
					return false;
				}
				const double extent = value.get<double>();
				if (!std::isfinite(extent) || extent < VRDepthCullingEnablePolicy::kMinimumExtent ||
					extent > VRDepthCullingEnablePolicy::kMaximumExtent) {
					a_error = "Depth-culling extent must be finite and in [0,1000]: " + name;
					return false;
				}
				(name == "exteriorMinExtent" ? update.exteriorMinExtent : update.interiorMinExtent) = static_cast<float>(extent);
			} else {
				a_error = "Unknown depth-culling field: " + name;
				return false;
			}
		}
		a_update = update;
		a_error.clear();
		return true;
	}

	/** Apply a validated partial update on the main thread, preserving unspecified settings. */
	template <class Settings>
	void Apply(const Update& a_update, Settings& a_settings)
	{
		if (a_update.exteriorEnabled)
			a_settings.EnableDepthBufferCullingExterior = *a_update.exteriorEnabled;
		if (a_update.interiorEnabled)
			a_settings.EnableDepthBufferCullingInterior = *a_update.interiorEnabled;
		if (a_update.exteriorMinExtent)
			a_settings.MinOccludeeBoxExtentExterior = *a_update.exteriorMinExtent;
		if (a_update.interiorMinExtent)
			a_settings.MinOccludeeBoxExtentInterior = *a_update.interiorMinExtent;
	}
}

#endif
