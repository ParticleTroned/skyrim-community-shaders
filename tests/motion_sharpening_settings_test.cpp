#include "Features/Upscaling/MotionSharpeningSettings.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using nlohmann::json;

	struct Settings
	{
		uint32_t qualityMode = 0;
		float sharpnessDLSS = 0.9f;
		bool motionAdaptiveRCAS = false;
		float motionSharpnessAdjustment = -0.5f;
		float motionSharpnessThreshold = 2.0f;
		float motionSharpnessCap = 1.0f;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, qualityMode, sharpnessDLSS,
		motionAdaptiveRCAS, motionSharpnessAdjustment, motionSharpnessThreshold, motionSharpnessCap)

	void Check(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Optional motion settings prevented safe enclosing settings deserialization");
	}

	Settings Load(json& profile, bool malformed)
	{
		Check(MotionSharpening::NormalizeLoadedSettings(profile) == malformed);
		const Settings settings = profile;
		Check(settings.qualityMode == 3 && settings.sharpnessDLSS == 0.25f);
		Check(profile.at("unrelated") == json({ { "nested", json::array({ 1, "retained", false }) } }));
		Check(MotionSharpening::IsValid({ settings.motionAdaptiveRCAS, settings.motionSharpnessAdjustment,
			settings.motionSharpnessThreshold, settings.motionSharpnessCap }));
		return settings;
	}
}

int main()
{
	try {
		const json original{ { "qualityMode", 3 }, { "sharpnessDLSS", 0.25 },
			{ "unrelated", { { "nested", json::array({ 1, "retained", false }) } } } };
		const char* keys[] = { "motionAdaptiveRCAS", "motionSharpnessAdjustment", "motionSharpnessThreshold", "motionSharpnessCap" };
		for (const auto* key : keys) {
			for (const auto& bad : { json(nullptr), json("malformed"), json::object(), json::array() }) {
				auto profile = original;
				profile[key] = bad;
				const auto loaded = Load(profile, true);
				Check(!loaded.motionAdaptiveRCAS);
			}
		}
		for (const auto* key : { keys[1], keys[2], keys[3] }) {
			for (const auto& bad : { json(true), json(std::numeric_limits<double>::quiet_NaN()), json(std::numeric_limits<double>::infinity()) }) {
				auto profile = original;
				profile[key] = bad;
				Load(profile, true);
			}
		}
		auto profile = original;
		profile[keys[0]] = 1;
		Check(!Load(profile, true).motionAdaptiveRCAS);
		profile = original;
		Check(!Load(profile, false).motionAdaptiveRCAS);
		profile[keys[0]] = true;
		profile[keys[1]] = -1e100;
		profile[keys[2]] = 1e100;
		profile[keys[3]] = 1e100;
		const auto huge = Load(profile, false);
		Check(huge.motionAdaptiveRCAS && huge.motionSharpnessAdjustment == -1.0f &&
			  huge.motionSharpnessThreshold == 64.0f && huge.motionSharpnessCap == 1.0f);
		const json saved = huge;
		const Settings restored = saved;
		Check(json(restored) == saved);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
