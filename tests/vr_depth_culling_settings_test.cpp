#include "Features/VRDepthCullingSettings.h"

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
			defaults["MinOccludeeBoxExtentExterior"] != 10.0f ||
			defaults["MinOccludeeBoxExtentInterior"] != 10.0f)
			return false;

		json old = { { "MinOccludeeBoxExtent", 83.5f }, { "DepthCullingLegacyMode", true } };
		return !NormalizeLoadedSettings(old) &&
		       old["MinOccludeeBoxExtentExterior"] == 83.5f &&
		       old["MinOccludeeBoxExtentInterior"] == 83.5f &&
		       !old.contains("MinOccludeeBoxExtent") &&
		       old["DepthCullingLegacyMode"].get<bool>();
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
			{ "EnableDepthBufferCullingInterior", true }, { "DepthCullingLegacyMode", true },
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

	bool CoversRetiredMethodSettings()
	{
		for (const bool legacy : { false, true }) {
			for (const auto& retiredMethod : json::array({ 0, 2, 3, -1, "invalid", nullptr })) {
				json saved = {
					{ "DepthCullingMethod", retiredMethod },
					{ "DepthCullingLegacyMode", legacy },
					{ "EnableDepthBufferCullingExterior", false },
					{ "EnableDepthBufferCullingInterior", true },
					{ "MinOccludeeBoxExtentExterior", 31.0f },
					{ "MinOccludeeBoxExtentInterior", 7.0f }
				};
				const auto original = saved;
				if (NormalizeLoadedSettings(saved) || saved != original)
					return false;
				json roundTrip = json::parse(saved.dump());
				if (NormalizeLoadedSettings(roundTrip) || roundTrip != original)
					return false;
			}
		}
		return true;
	}
}

int main()
{
	return CoversDefaultsAndSharedThresholdMigration() &&
	               CoversIndependentRoundTripAndDisabledMasterMigration() &&
	               CoversInvalidExtentsAndIndependentPrecedence() &&
	               CoversRetiredMethodSettings() ?
	           0 :
	           1;
}
