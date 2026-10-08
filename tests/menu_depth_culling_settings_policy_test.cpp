#include "MenuDepthCullingSettingsPolicy.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using MenuDepthCullingSettingsPolicy::Apply;
	using MenuDepthCullingSettingsPolicy::TryParse;
	using MenuDepthCullingSettingsPolicy::Update;
	using nlohmann::json;

	struct Settings
	{
		bool EnableDepthBufferCullingExterior = true;
		bool EnableDepthBufferCullingInterior = false;
		float MinOccludeeBoxExtentExterior = 10.0f;
		float MinOccludeeBoxExtentInterior = 20.0f;
		bool DepthCullingLegacyMode = true;
	};

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void TestIndependentUpdates()
	{
		Settings settings;
		Update update;
		std::string error;
		Require(TryParse({ { "interiorEnabled", true }, { "interiorMinExtent", 1000 } }, update, error), "Interior update rejected");
		Apply(update, settings);
		Require(settings.EnableDepthBufferCullingExterior && settings.MinOccludeeBoxExtentExterior == 10.0f, "Interior update changed exterior");
		Require(settings.EnableDepthBufferCullingInterior && settings.MinOccludeeBoxExtentInterior == 1000.0f, "Interior values not applied");
		Require(TryParse({ { "exteriorEnabled", false }, { "exteriorMinExtent", 0.0 } }, update, error), "Exterior update rejected");
		Apply(update, settings);
		Require(!settings.EnableDepthBufferCullingExterior && settings.MinOccludeeBoxExtentExterior == 0.0f, "False/zero values were lost");
		Require(settings.EnableDepthBufferCullingInterior && settings.MinOccludeeBoxExtentInterior == 1000.0f, "Exterior update changed interior");
		Require(settings.DepthCullingLegacyMode, "Independent controls changed selected mode");
		Require(error.empty(), "Successful parse retained error");
	}

	void TestInvalidUpdates()
	{
		const json invalidValues[] = {
			json(), json::object(), json::array(), true, "settings", 12,
			{ { "exteriorEnabled", 1 } }, { { "interiorEnabled", "true" } },
			{ { "interiorMinExtent", false } }, { { "exteriorMinExtent", "10" } },
			{ { "interiorMinExtent", nullptr } }, { { "exteriorMinExtent", -0.001 } },
			{ { "interiorMinExtent", 1000.001 } },
			{ { "exteriorMinExtent", std::numeric_limits<double>::infinity() } },
			{ { "interiorMinExtent", -std::numeric_limits<double>::infinity() } },
			{ { "exteriorMinExtent", std::numeric_limits<double>::quiet_NaN() } },
			{ { "exteriorEnabled", false }, { "interiorMinExtent", 1001 } },
			{ { "exteriorEnabled", false }, { "unknown", true } },
			{ { "performanceMode", true } }, { { "legacyMode", false } }
		};
		for (const auto& value : invalidValues) {
			Update update{ true, false, 12.0f, 34.0f };
			std::string error;
			Require(!TryParse(value, update, error), "Invalid update accepted");
			Require(!error.empty(), "Invalid update has no explanation");
			Require(update.exteriorEnabled == true && update.interiorEnabled == false &&
						update.exteriorMinExtent == 12.0f && update.interiorMinExtent == 34.0f,
				"Rejected update partially modified staged values");
		}
	}

	void TestMethodIdentifiers()
	{
		using VRDepthCullingTemporal::Mode;
		Mode method = Mode::Balanced;
		std::string error = "previous error";
		for (const auto expected : { Mode::Balanced, Mode::Legacy, Mode::Hybrid }) {
			Require(MenuDepthCullingSettingsPolicy::TryParseMethod(VRDepthCullingTemporal::GetModeName(expected), method, error),
				"Supported method identifier rejected");
			Require(method == expected && error.empty(), "Method parse returned an incorrect result");
		}
		const json invalidMethods[] = { nullptr, true, 0, 2, 3, "", "advanced", "Hybrid", "performance", json::object(), json::array() };
		for (const auto& value : invalidMethods) {
			method = Mode::Hybrid;
			Require(!MenuDepthCullingSettingsPolicy::TryParseMethod(value, method, error), "Invalid method identifier accepted");
			Require(method == Mode::Hybrid && !error.empty(), "Rejected method modified the selected value or omitted its error");
		}
	}
}

int main()
{
	try {
		TestIndependentUpdates();
		TestInvalidUpdates();
		TestMethodIdentifiers();
		std::cout << "Depth-culling DevBench update validation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
