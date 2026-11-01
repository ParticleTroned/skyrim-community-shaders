#include "Features/Upscaling/NeuralRendering/MonoSettingsSerialization.h"

#include <cassert>
#include <iostream>
#include <limits>

int main()
{
	using namespace NeuralRendering;
	using Json = nlohmann::json;
	MonoSettings settings;
	const auto original = settings;
	assert(ApplyMonoSettingsPatch({ { "enabled", true }, { "mode", 2 }, { "modelResolutionPercent", 75 },
		{ "localTone", 1.1 }, { "regionOffset", { -.2, .1 } } }, settings).empty());
	assert(settings.enabled && settings.mode == 2 && settings.modelResolutionPercent == 75 && settings.localTone == 1.1f);
	const auto valid = settings;
	const auto roundTrip = Json(settings).get<MonoSettings>();
	assert(roundTrip == settings);
	for (const auto& patch : {
		Json::object(), Json::array(), Json{ { "unknown", true } }, Json{ { "enabled", 1 } },
		Json{ { "mode", 1 } }, Json{ { "mode", -1 } }, Json{ { "mode", 0x100000002ull } },
		Json{ { "modelResolutionPercent", 29 } }, Json{ { "modelResolutionPercent", 101 } },
		Json{ { "modelResolutionPercent", 75.0 } }, Json{ { "network", 3 } }, Json{ { "style", 3 } },
		Json{ { "intensity", 2.1 } }, Json{ { "localTone", std::numeric_limits<double>::quiet_NaN() } },
		Json{ { "skinStructure", 1e100 } }, Json{ { "categoryMask", 0xff } },
		Json{ { "categoryStrengths", { 1, 1, 1, 1, 2 } } }, Json{ { "categoryStrengths", { 1, 1 } } },
		Json{ { "regionOffset", { 0, .6 } } }, Json{ { "regionOffset", { "0", 0 } } },
		Json{ { "enabled", false }, { "maximumDistanceMeters", -1 } } }) {
		assert(!ApplyMonoSettingsPatch(patch, settings).empty());
		assert(settings == valid);
	}
	assert(ApplyMonoSettingsPatch(Json(original), settings).empty());
	assert(settings == original);
	std::cout << "Mono NR serialization and strict atomic settings validation passed\n";
}
