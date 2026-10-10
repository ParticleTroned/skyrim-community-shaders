#include "MenuDepthOfFieldSettingsPolicy.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using AdaptiveBalanceDepthOfField::Settings;
	using nlohmann::json;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void TestIndependentPartialUpdates()
	{
		Settings settings;
		settings.sceneDof.values.distance = 1100.0f;
		settings.underwaterDof.values.strength = 0.7f;
		std::string error;
		Require(MenuDepthOfFieldSettingsPolicy::Apply({ { "fixUnderwaterFogDofBlur", true } }, settings, error), "Correction update rejected");
		Require(!settings.enabled && settings.fixUnderwaterFogDofBlur, "Correction depends on manual override enable");
		Require(settings.sceneDof.values.distance == 1100.0f && settings.underwaterDof.values.strength == 0.7f, "Correction changed blur settings");
		const json autofocus{ { "farDistance", 10000 }, { "blurMultiplier", 0.0 } };
		const json values{ { "autoFocus", true }, { "strength", 1.0 }, { "mode", 3 }, { "blurRadius", 7 }, { "autoFocusSettings", autofocus } };
		const json scene{ { "locked", true }, { "values", values } };
		Require(MenuDepthOfFieldSettingsPolicy::Apply({ { "enabled", true }, { "sceneDof", scene } }, settings, error), "Valid nested update rejected");
		Require(settings.enabled && settings.sceneDof.locked && settings.sceneDof.values.autoFocus, "Enable, lock or autofocus lost");
		Require(settings.sceneDof.values.mode == 3 && settings.sceneDof.values.blurRadius == 7, "Packed mode limits rejected");
		Require(settings.sceneDof.values.autoFocusSettings.farDistance == 10000 && settings.sceneDof.values.autoFocusSettings.blurMultiplier == 0, "Autofocus limits or zero lost");
		Require(settings.sceneDof.values.distance == 1100.0f && settings.underwaterDof.values.strength == 0.7f, "Nested patch reset omitted fields");
		Require(MenuDepthOfFieldSettingsPolicy::Apply({ { "enabled", false }, { "sceneDof", { { "locked", false }, { "values", { { "strength", 0 }, { "mode", 0 }, { "blurRadius", 0 }, { "excludeSky", false } } } } } }, settings, error), "False and zero update rejected");
		Require(!settings.enabled && settings.fixUnderwaterFogDofBlur, "Manual disable changed correction");
		Require(!settings.sceneDof.locked && settings.sceneDof.values.strength == 0 && settings.sceneDof.values.mode == 0 && settings.sceneDof.values.blurRadius == 0, "False and zero not applied");
		Require(error.empty(), "Successful update retained error");
		const json roundTrip = settings;
		Require(MenuDepthOfFieldSettingsPolicy::Apply(roundTrip, settings, error), "Full settings restore rejected");
		Require(json(settings) == roundTrip, "Full settings restore changed values");
	}

	void TestRejectedUpdatesAreAtomic()
	{
		Settings settings;
		settings.sceneDof.values.distance = 77;
		const json before = settings;
		const json invalid[]{
			nullptr, true, json::array(), json::object(),
			{ { "enabled", 1 } }, { { "fixUnderwaterFogDofBlur", nullptr } },
			{ { "unexpected", 1 } }, { { "sceneDof", json::object() } },
			{ { "sceneDof", { { "values", { { "typo", 0.5 } } } } } },
			{ { "sceneDof", { { "values", { { "strength", 1.001 } } } } } },
			{ { "sceneDof", { { "values", { { "distance", -1 } } } } } },
			{ { "underwaterDof", { { "values", { { "range", 50001 } } } } } },
			{ { "underwaterDof", { { "values", { { "mode", 4 } } } } } },
			{ { "sceneDof", { { "values", { { "mode", 2.5 } } } } } },
			{ { "sceneDof", { { "values", { { "blurRadius", 8 } } } } } },
			{ { "sceneDof", { { "values", { { "autoFocusSettings", { { "nearDistance", 10001 } } } } } } } },
			{ { "sceneDof", { { "values", { { "autoFocusSettings", { { "blurMultiplier", -0.01 } } } } } } } },
			{ { "sceneDof", { { "baseline", { { "strength", "0.5" } } } } } },
			{ { "sceneDof", { { "values", { { "strength", std::numeric_limits<double>::infinity() } } } } } },
			{ { "sceneDof", { { "values", { { "range", std::numeric_limits<double>::quiet_NaN() } } } } } },
			{ { "enabled", true }, { "fixUnderwaterFogDofBlur", true }, { "underwaterDof", { { "values", { { "distance", 50001 } } } } } }
		};
		for (const auto& patch : invalid) {
			std::string error;
			Require(!MenuDepthOfFieldSettingsPolicy::Apply(patch, settings, error), "Invalid patch was accepted");
			Require(json(settings) == before, "Rejected patch partially changed saved settings");
			Require(!error.empty(), "Rejected patch did not explain failure");
		}
	}
}

int main()
{
	try {
		TestIndependentPartialUpdates();
		TestRejectedUpdatesAreAtomic();
		std::cout << "DOF settings policy passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
