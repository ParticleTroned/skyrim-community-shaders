#include "Features/Upscaling/FSRTemporalTuningSerialization.h"

#include <iostream>
#include <limits>
#include <string>

namespace logger
{
	unsigned warnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++warnings;
	}
}
namespace FSRTemporalTuningPolicy
{
	using json = nlohmann::json;
#include "temporal_settings_load_under_test.h"
}
namespace
{
	using json = nlohmann::json;
	using namespace FSRTemporalTuningPolicy;
	struct Profile
	{
		int unrelated = 21;
		Settings tuning{};
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Profile, unrelated, tuning)

	int failures = 0;
	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	}
}

int main()
{
	const json malformed[]{ nullptr, false, 1, "invalid", json::array(),
		{ { "enabled", 1 } }, { { "enabled", true }, { "velocityFactor", "bad" } },
		{ { "enabled", true }, { "reactivenessScale", nullptr } },
		{ { "enabled", true }, { "shadingChangeScale", false } },
		{ { "enabled", true }, { "accumulationAddedPerFrame", 2.0 } },
		{ { "enabled", true }, { "minimumDisocclusionAccumulation", -1.000000001 } } };
	for (const auto& value : malformed) {
		const auto before = logger::warnings;
		const auto profile = json{ { "unrelated", 47 }, { "tuning", value } }.get<Profile>();
		Check(profile.unrelated == 47 && profile.tuning == Settings{}, "malformed tuning must disable only its own profile");
		Check(logger::warnings == before + 1, "invalid loaded tuning must report its fallback");
	}
	Settings original{};
	original.enabled = true;
	original.velocityFactor = 0.5f;
	for (const auto& value : malformed) {
		auto candidate = original;
		Check(ApplySettingsPatch(value, candidate) && candidate == original, "invalid live patch must preserve every requested setting");
	}
	for (const auto& field : kNumericSettings) {
		for (const double invalid : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
				 static_cast<double>(field.maximum) + 0.000000001, static_cast<double>(field.minimum) - 0.000000001 }) {
			auto candidate = original;
			Check(ApplySettingsPatch(json{ { field.name, invalid } }, candidate) && candidate == original,
				"nonfinite and just-outside-bound numbers must fail before float narrowing");
		}
	}
	auto patched = original;
	Check(!ApplySettingsPatch(json{ { "shadingChangeScale", 2.0 } }, patched) && patched.shadingChangeScale == 2.0f && patched.velocityFactor == 0.5f,
		"partial live patches must preserve unmentioned settings");
	Check(ApplySettingsPatch(json{ { "futureSetting", true } }, patched) && patched.shadingChangeScale == 2.0f,
		"unknown live keys must be rejected");
	const auto loaded = json{ { "enabled", true }, { "velocityFactor", 0.5 }, { "futureSetting", 99 } }.get<Settings>();
	Check(loaded == original, "loaded unknown keys must retain forward compatibility");
	Check(json(original).get<Settings>() == original, "persisted profile must round trip");
	Check(json{ { "unrelated", 47 } }.get<Profile>().tuning == Settings{}, "older configurations retain vendor defaults");
	return failures ? 1 : 0;
}
