#include "Features/VRDepthCullingSettings.h"

#include <cstdint>
#include <limits>

namespace
{
	using nlohmann::json;
	using VRDepthCullingSettings::NormalizeLoadedSettings;

	bool CoversDefaultsAndSharedThresholdMigration()
	{
		json defaults = json::object();
		if (NormalizeLoadedSettings(defaults) ||
			!defaults["EnableDepthBufferCullingExterior"].get<bool>() ||
			!defaults["EnableDepthBufferCullingInterior"].get<bool>() ||
			defaults["DepthCullingLegacyMode"].get<bool>() ||
			defaults["DepthCullingMethod"] != 0 ||
			defaults["MinOccludeeBoxExtentExterior"] != 10.0f ||
			defaults["MinOccludeeBoxExtentInterior"] != 10.0f)
			return false;

		json old = { { "MinOccludeeBoxExtent", 83.5f }, { "DepthCullingLegacyMode", true } };
		return !NormalizeLoadedSettings(old) &&
		       old["MinOccludeeBoxExtentExterior"] == 83.5f &&
		       old["MinOccludeeBoxExtentInterior"] == 83.5f &&
		       !old.contains("MinOccludeeBoxExtent") &&
		       old["DepthCullingLegacyMode"].get<bool>() && old["DepthCullingMethod"] == 2;
	}

	bool CoversIndependentRoundTripAndDisabledMasterMigration()
	{
		json old = { { "EnableDepthBufferCullingExterior", false },
			{ "EnableDepthBufferCullingInterior", true }, { "MinOccludeeBoxExtent", 22.0f },
			{ "DepthCullingLegacyMode", false } };
		if (NormalizeLoadedSettings(old) || old["EnableDepthBufferCullingInterior"].get<bool>())
			return false;

		json independentLegacy = { { "EnableDepthBufferCullingExterior", false },
			{ "EnableDepthBufferCullingInterior", true }, { "MinOccludeeBoxExtent", 32.0f } };
		if (NormalizeLoadedSettings(independentLegacy) || !independentLegacy["EnableDepthBufferCullingInterior"].get<bool>())
			return false;

		json split = { { "EnableDepthBufferCullingExterior", false },
			{ "EnableDepthBufferCullingInterior", true }, { "DepthCullingLegacyMode", true }, { "DepthCullingMethod", 2 },
			{ "MinOccludeeBoxExtentExterior", 70.0f }, { "MinOccludeeBoxExtentInterior", 12.0f } };
		const json original = split;
		if (NormalizeLoadedSettings(split) || split != original)
			return false;
		json restored = json::parse(split.dump());
		return !NormalizeLoadedSettings(restored) && restored == original;
	}

	bool CoversInvalidExtentsAndIndependentPrecedence()
	{
		json settings = { { "MinOccludeeBoxExtent", 17.0f },
			{ "MinOccludeeBoxExtentExterior", 9.0f } };
		if (NormalizeLoadedSettings(settings) || settings["MinOccludeeBoxExtentExterior"] != 9.0f ||
			settings["MinOccludeeBoxExtentInterior"] != 17.0f)
			return false;
		settings["MinOccludeeBoxExtentExterior"] = -1.0;
		settings["MinOccludeeBoxExtentInterior"] = std::numeric_limits<double>::max();
		if (!NormalizeLoadedSettings(settings) || settings["MinOccludeeBoxExtentExterior"] != 0.0f ||
			settings["MinOccludeeBoxExtentInterior"] != 1000.0f)
			return false;
		settings["MinOccludeeBoxExtentExterior"] = std::numeric_limits<double>::infinity();
		settings["MinOccludeeBoxExtentInterior"] = "invalid";
		settings["DepthCullingLegacyMode"] = "invalid";
		return NormalizeLoadedSettings(settings) &&
		       settings["MinOccludeeBoxExtentExterior"] == 10.0f &&
		       settings["MinOccludeeBoxExtentInterior"] == 10.0f &&
		       !settings["DepthCullingLegacyMode"].get<bool>();
	}

	bool CoversMethodMigrationAndValidation()
	{
		for (const int method : { 0, 2, 3 }) {
			json settings = { { "DepthCullingMethod", method }, { "DepthCullingLegacyMode", method != 2 },
				{ "EnableDepthBufferCullingExterior", false }, { "EnableDepthBufferCullingInterior", true } };
			if (NormalizeLoadedSettings(settings) || settings["DepthCullingMethod"] != method ||
				settings["DepthCullingLegacyMode"] != (method == 2) || !settings["EnableDepthBufferCullingInterior"].get<bool>())
				return false;
			const auto restored = json::parse(settings.dump());
			if (NormalizeLoadedSettings(settings) || settings != restored)
				return false;
		}
		const json invalidMethods[] = { nullptr, true, "hybrid", 1, -1, 4, 3.0,
			std::numeric_limits<std::uint64_t>::max(), json::object(), json::array() };
		for (const auto& method : invalidMethods) {
			json settings = { { "DepthCullingMethod", method }, { "DepthCullingLegacyMode", true } };
			if (!NormalizeLoadedSettings(settings) || settings["DepthCullingMethod"] != 0 ||
				settings["DepthCullingLegacyMode"].get<bool>())
				return false;
		}
		json retired = { { "DepthCullingPerformanceMode", true } };
		return !NormalizeLoadedSettings(retired) && retired["DepthCullingMethod"] == 0;
	}
}

int main()
{
	return CoversDefaultsAndSharedThresholdMigration() &&
	               CoversIndependentRoundTripAndDisabledMasterMigration() &&
	               CoversInvalidExtentsAndIndependentPrecedence() &&
	               CoversMethodMigrationAndValidation() ?
	           0 :
	           1;
}
