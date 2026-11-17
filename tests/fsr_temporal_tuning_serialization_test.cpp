#include "Features/Upscaling/ColourPipelineSceneObservation.h"
#include "Features/Upscaling/FSRDispatchInputTelemetry.h"
#include "Features/Upscaling/FSRTemporalTuningSerialization.h"

#include <iostream>
#include <limits>
#include <stdexcept>
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

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			throw std::runtime_error("FSR submitted input telemetry regression");
	}

	void RequireUnavailable(const nlohmann::json& a_result)
	{
		Require(a_result.size() == 5 && a_result.at("schemaVersion") == 1);
		Require(a_result.at("available").is_boolean() && !a_result.at("available").get<bool>());
		Require(a_result.at("reset").is_null() && a_result.at("jitterOffsetPixels").is_null() &&
				a_result.at("frameTimeDeltaMilliseconds").is_null());
		Require(nlohmann::json::parse(a_result.dump()) == a_result);
	}
}

void TestSubmittedInputTelemetry()
{
	namespace Inputs = FSRDispatchInputTelemetry;
	for (bool reset : { false, true }) {
		const auto snapshot = Inputs::Capture(reset, -0.25f, 0.375f, 16.5f);
		const auto result = Inputs::ToJson(snapshot, true);
		Require(result.size() == 5 && result.at("schemaVersion") == 1);
		Require(result.at("available").is_boolean() && result.at("available").get<bool>());
		Require(result.at("reset").is_boolean() && result.at("reset").get<bool>() == reset);
		Require(result.at("jitterOffsetPixels").is_array() && result.at("jitterOffsetPixels").size() == 2);
		Require(result.at("jitterOffsetPixels").at(0) == -0.25f && result.at("jitterOffsetPixels").at(1) == 0.375f);
		Require(result.at("frameTimeDeltaMilliseconds").is_number_float() &&
				result.at("frameTimeDeltaMilliseconds") == 16.5f);
		Require(nlohmann::json::parse(result.dump()) == result);
		RequireUnavailable(Inputs::ToJson(snapshot, false));
	}
	RequireUnavailable(Inputs::ToJson({}, true));
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(),
			 std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		RequireUnavailable(Inputs::ToJson(Inputs::Capture(true, invalid, 0.0f, 10.0f), true));
		RequireUnavailable(Inputs::ToJson(Inputs::Capture(true, 0.0f, invalid, 10.0f), true));
		RequireUnavailable(Inputs::ToJson(Inputs::Capture(true, 0.0f, 0.0f, invalid), true));
		RequireUnavailable(Inputs::ToJson({ true, true, invalid, 0.0f, 10.0f }, true));
	}
	RequireUnavailable(Inputs::ToJson(Inputs::Capture(false, 0.0f, 0.0f, -1.0f), true));
	const auto zero = Inputs::ToJson(Inputs::Capture(false, 0.0f, -0.0f, 0.0f), true);
	Require(zero.at("available") == true && zero.at("reset") == false &&
			zero.at("frameTimeDeltaMilliseconds") == 0.0f);
	const auto maximum = std::numeric_limits<float>::max();
	Require(Inputs::ToJson(Inputs::Capture(true, maximum, -maximum, maximum), true).at("available") == true);
}

void TestSceneObservation()
{
	namespace Scene = CSX::Diagnostics::ColourPipelineSceneObservation;
	const auto missing = Scene::ToJson({});
	Require(missing.at("schemaVersion") == 1 && missing.at("available") == false);
	Require(missing.at("observationCpuFrame").is_null() && missing.at("beginQpc").is_null());
	Require(missing.at("cameraCache").at("view").is_null());
	Require(missing.at("imageSpaceParameters").at("hdr").at("white").is_null());
	Scene::Snapshot snapshot{};
	snapshot.cpuFrame = 41;
	snapshot.eye = 1;
	snapshot.beginQpc = 1000;
	snapshot.endQpc = 1100;
	snapshot.qpcFrequency = 10000000;
	snapshot.lastStartedWorldFrame = 41;
	snapshot.lastCompletedWorldFrame = 40;
	for (std::size_t index = 0; index < snapshot.matrices.size(); ++index)
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				snapshot.matrices[index][row][column] = static_cast<float>(index * 100 + row * 10 + column);
	snapshot.positionAdjust = { 1.0f, 2.0f, 3.0f };
	snapshot.previousPositionAdjust = { 4.0f, 5.0f, 6.0f };
	snapshot.imageSpaceAvailable = true;
	snapshot.hdr = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	snapshot.cinematic = { 10, 11, 12 };
	snapshot.tint = { 0.25f, 0.1f, 0.2f, 0.3f };
	const auto retained = Scene::ToJson(snapshot);
	Require(retained.at("available") == true && retained.at("eyeIndex") == 1);
	Require(retained.at("observationCpuFrame") == 41 && retained.at("endQpc") == 1100);
	Require(retained.at("cameraCache").at("view").at(1).at(2) == 12.0f);
	Require(retained.at("cameraCache").at("projection").at(1).at(2) == 112.0f);
	Require(retained.at("cameraCache").at("contentCpuFrame").is_null());
	Require(retained.at("cameraCache").at("initialized").is_null());
	Require(retained.at("worldRender").at("lastCompletedCpuFrame") == 40);
	Require(retained.at("worldRender").at("continuousCameraEquivalenceProven") == false);
	Require(retained.at("imageSpaceParameters").at("hdr").at("skyScale") == 9.0f);
	Require(retained.at("imageSpaceParameters").at("internalAdaptiveExposure").at("value").is_null());
	Require(nlohmann::json::parse(retained.dump()) == retained && retained.dump().size() < 16384);
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		auto changed = snapshot;
		changed.matrices[2][3][1] = invalid;
		auto result = Scene::ToJson(changed);
		Require(result.at("cameraCache").at("available") == false && result.at("cameraCache").at("projection").is_null());
		changed = snapshot;
		changed.hdr[7] = invalid;
		result = Scene::ToJson(changed);
		Require(result.at("imageSpaceParameters").at("hdr").at("available") == false);
		Require(result.at("imageSpaceParameters").at("hdr").at("sunlightScale").is_null());
		Require(result.at("imageSpaceParameters").at("cinematic").at("available") == true);
		changed = snapshot;
		changed.positionAdjust[1] = invalid;
		Require(Scene::ToJson(changed).at("cameraCache").at("view").is_null());
	}
	auto changed = snapshot;
	changed.imageSpaceAvailable = false;
	Require(Scene::ToJson(changed).at("imageSpaceParameters").at("tint").at("rgb").is_null());
	changed = snapshot;
	changed.eye = 2;
	Require(Scene::ToJson(changed).at("available") == false);
	changed = snapshot;
	changed.endQpc = 999;
	Require(Scene::ToJson(changed).at("available") == false);
	snapshot.cpuFrame = 99;
	snapshot.matrices[0][1][2] = 900.0f;
	Require(retained.at("observationCpuFrame") == 41 && retained.at("cameraCache").at("view").at(1).at(2) == 12.0f);
}

int main()
{
	TestSceneObservation();
	TestSubmittedInputTelemetry();
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
