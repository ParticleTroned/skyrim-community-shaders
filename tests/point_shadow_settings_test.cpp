#include "Api/ServiceFoundation.h"
#include "Features/LightLimitFix/LightAttachPolicy.h"
#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Menu/PerformanceTuningController.h"
#include "Utils/Finite.h"
#include "Utils/PointShadowCorrection.h"

#include <iostream>
#include <limits>

using json = nlohmann::json;
using uint = unsigned;
using Mode = PointShadowCorrection::Mode;

struct State
{
	std::uint32_t frameCount = 0;
	bool save = false, engine = false, menu = false, pendingPostLoadRuntimeReset = false, disabled = false;
	mutable unsigned disabledQueries = 0;
	bool IsSaveLoadSafeModeActive() const { return save; }
	bool IsEngineSaveLoadActivityActive() const { return engine; }
	bool IsMainOrLoadingMenuOpen() const { return menu; }
	bool IsFeatureDisabled(const std::string&) const
	{
		++disabledQueries;
		return disabled;
	}
};
struct Profiler
{
	enum class CaptureSessionState
	{
		None,
		Running
	};
	CaptureSessionState state = CaptureSessionState::None;
	Profiler GetBoundedCaptureProgress() const { return *this; }
};
struct PerformanceTuningRenderer
{
	inline static bool active = false;
	static bool HasActiveMeasurements() { return active; }
};
namespace globals
{
	inline Profiler profilerInstance;
	inline Profiler* profiler = &profilerInstance;
	inline State instance;
	inline State* state = &instance;
}
namespace LightLimitFixDetail::LightAttach
{
	void Invalidate() {}
}

struct LightLimitFix
{
	Util::RuntimeToggle runtimeToggle{ false };
	PointShadowCorrection::RuntimeState pointShadowCorrection;
	bool loaded = true;
	bool IsContactShadowsRequested() const { return runtimeToggle.Get(); }
	void SetContactShadowsEnabled(bool value) { runtimeToggle.Set(value); }
#include "point_shadow_accessors.h"
#include "point_shadow_settings.h"
	Settings settings;
#include "point_shadow_performance.h"
	json CapturePerformanceSettingsState() const;
	void ReplaceSettings(Settings);
	void LoadSettings(json&);
	void SaveSettings(json&);
	void RestoreDefaultSettings();
	void RestorePerformanceToggleState(const json& value) { RestorePerformanceCostMeasurementState(value); }
	void Frame()
	{
		++globals::state->frameCount;
		ApplyPointShadowCorrection();
		runtimeToggle.Apply(settings.EnableContactShadows, globals::state);
	}
};
constexpr uint CONTACT_SHADOW_MAX_LIGHTS = 8;
// Extracted definitions depend on the serializer and sanitizer declarations.
// clang-format off
#include "point_shadow_constants.h"
#include "point_shadow_sanitize.h"
#include "point_shadow_serialization.h"
#include "point_shadow_publication.h"
#include "point_shadow_capture.h"
#include "point_shadow_load.h"
// clang-format on
namespace globals::features
{
	inline LightLimitFix lightLimitFix;
}
#include "point_shadow_api.h"

CSX::Api::ServiceFoundation& Foundation()
{
	static CSX::Api::ServiceFoundation service({ "csx.feature", 1, 1, 45 });
	return service;
}
template <class Task>
json OnMain(Task task)
{
	return task();
}
// Exercise the production action and its admission guard through the real envelope.
// clang-format off
#include "point_shadow_dispatch_start.h"
#include "point_shadow_dispatch_action.h"
    return Foundation().MakeError(args, "unsupported_action", "Unknown action");
}
// clang-format on

json Request(json args = json::object())
{
	static unsigned ordinal = 0;
	args["action"] = args.value("action", std::string("point_shadow_correction"));
	args["contractMajor"] = 1;
	args["clientId"] = "point-shadow-test";
	args["commandId"] = std::to_string(++ordinal);
	args["expectedBuildId"] = "test-build";
	return args;
}
json Dispatch(json args = json::object())
{
	return Foundation().Dispatch(Request(std::move(args)), BuildResult);
}

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
template <class Function>
void Reject(Function&& run)
{
	bool rejected = false;
	try {
		run();
	} catch (const std::exception&) {
		rejected = true;
	}
	Require(rejected, "Invalid input must be rejected");
}

int main()
try {
	{
		LightLimitFix startup;
		Require(startup.GetRequestedPointShadowCorrection() == Mode::Off && startup.GetAppliedPointShadowCorrection() == Mode::Off, "New instances start with correction Off");
		Require(startup.GetEffectivePointShadowCorrection() == Mode::Off && globals::instance.disabledQueries == 0,
			"Off must skip the feature-availability lookup");
		json clip{ { "PointShadowCorrection", 1 } };
		startup.LoadSettings(clip);
		Require(startup.GetEffectivePointShadowCorrection() == Mode::ClipOnly && globals::instance.disabledQueries == 1,
			"Clip must still check feature availability");
		json settings{ { "PointShadowCorrection", 0 }, { "EnableContactShadows", true } };
		startup.LoadSettings(settings);
		Require(startup.GetAppliedPointShadowCorrection() == Mode::Off && startup.settings.EnableContactShadows,
			"Complete startup load initializes both applied settings before rendering");
	}
	auto& feature = globals::features::lightLimitFix;
	feature.SetPerformanceToggleEnabled(false);
	feature.Frame();
	json legacy;
	feature.SaveSettings(legacy);
	legacy.erase("PointShadowCorrection");
	feature.LoadSettings(legacy);
	Require(feature.GetRequestedPointShadowCorrection() == Mode::Off && !feature.IsPerformanceToggleEnabled(), "Missing settings default to Off without enabling other components");
	Require(!feature.IsContactShadowsRequested() && !feature.settings.EnableParticleLights, "Defaulting preserves independent components");
	feature.Frame();

	for (const auto mode : { Mode::Off, Mode::ClipOnly }) {
		json settings = legacy;
		settings["PointShadowCorrection"] = mode;
		feature.LoadSettings(settings);
		json saved;
		feature.SaveSettings(saved);
		Require(saved.at("PointShadowCorrection").get<Mode>() == mode, "Save must retain pending request");
		Require(feature.CapturePerformanceSettingsState().at("PointShadowCorrection") == saved.at("PointShadowCorrection"), "Both snapshot routes retain requested mode");
		Require(feature.CapturePerformanceCostMeasurementState().at("PointShadowCorrection") == saved.at("PointShadowCorrection"), "Cost snapshot retains requested mode");
		feature.Frame();
		Require(feature.GetAppliedPointShadowCorrection() == mode && feature.GetEffectivePointShadowCorrection() == mode && !feature.IsPointShadowCorrectionPending(), "Each valid mode settles");
	}
	for (const json invalid : { json(true), json(false), json(1.0), json(-1), json(2), json(3), json("full"), json(), json::array(), json::object(), json(std::numeric_limits<std::uint64_t>::max()) }) {
		auto settings = legacy;
		settings["EnableParticleLights"] = true;
		settings["PointShadowCorrection"] = invalid;
		const auto before = feature.CapturePerformanceSettingsState();
		Reject([&] { feature.LoadSettings(settings); });
		Require(feature.CapturePerformanceSettingsState() == before, "Invalid full input must not partially mutate settings");
	}
	for (const json invalid : {
			 json{ { "PointShadowCorrection", 2 }, { "EnableParticleLights", true } },
			 json{ { "PointShadowCorrection", 3 }, { "EnableParticleLights", true } },
			 json{ { "PointShadowCorrection", 0 }, { "EnableParticleLights", "invalid" } },
			 json{ { "PointShadowCorrection", 0 }, { "EnableContactShadows", true }, { "MaxParticleDistance", "invalid" } } }) {
		const auto before = feature.CapturePerformanceSettingsState();
		Reject([&] { feature.RestorePerformanceCostMeasurementState(invalid); });
		Require(feature.CapturePerformanceSettingsState() == before, "Invalid partial restoration preserves every setting and pending request");
	}
	Reject([&] { feature.SetPointShadowCorrection(static_cast<Mode>(2)); });
	Reject([&] { feature.SetPointShadowCorrection(static_cast<Mode>(3)); });
	feature.SetPointShadowCorrection(Mode::Off);
	feature.Frame();
	feature.SetPointShadowCorrection(Mode::ClipOnly);
	feature.RestorePerformanceCostMeasurementState(json{ { "EnableParticleLights", false } });
	Require(feature.GetRequestedPointShadowCorrection() == Mode::ClipOnly, "Partial snapshot updates preserve the preference");
	feature.SetContactShadowsEnabled(true);
	json simultaneous;
	feature.SaveSettings(simultaneous);
	feature.LoadSettings(simultaneous);
	Require(!feature.settings.EnableContactShadows && feature.settings.PointShadowCorrection == Mode::Off, "One replacement must preserve both applied fields");
	Require(feature.IsContactShadowsRequested() && feature.GetRequestedPointShadowCorrection() == Mode::ClipOnly, "One replacement must stage both requests");
	Require(!feature.IsPerformanceCostMeasurementReady(), "Pending requests block measurement readiness");
	for (bool* blocked : { &globals::instance.save, &globals::instance.engine, &globals::instance.menu, &globals::instance.pendingPostLoadRuntimeReset }) {
		*blocked = true;
		feature.Frame();
		Require(feature.GetAppliedPointShadowCorrection() == Mode::Off, "Unsafe frames defer mode application");
		*blocked = false;
		feature.ApplyPointShadowCorrection();
		Require(feature.GetAppliedPointShadowCorrection() == Mode::Off, "Clearing a guard cannot split a frame");
	}
	feature.Frame();
	Require(feature.IsPerformanceCostMeasurementReady() && feature.settings.EnableContactShadows && feature.GetAppliedPointShadowCorrection() == Mode::ClipOnly, "Both requests apply at the next safe boundary");
	std::optional<PerformanceTuningController::DisabledConfiguration> disabled;
	Require(PerformanceTuningController::SetEnabled(feature, false, disabled), "Overview disables all components");
	Require(feature.GetRequestedPointShadowCorrection() == Mode::Off && !feature.IsPerformanceToggleEnabled(), "Overview Off includes correction");
	Require(PerformanceTuningController::SetEnabled(feature, true, disabled), "Overview restores saved state");
	Require(feature.GetRequestedPointShadowCorrection() == Mode::ClipOnly, "Overview restore retains Clip only");
	feature.SetContactShadowsEnabled(false);
	feature.Frame();
	for (const auto mode : { Mode::Off, Mode::ClipOnly }) {
		feature.SetPointShadowCorrection(mode);
		feature.Frame();
		const auto before = feature.CapturePerformanceCostMeasurementState();
		feature.SetPerformanceCostMeasurementEnabled(false);
		feature.Frame();
		feature.RestorePerformanceCostMeasurementState(before);
		Require(feature.CapturePerformanceCostMeasurementState() == before, "Measurement restoration retains exact mode and components");
		Require(feature.IsPerformanceCostMeasurementReady() == (mode == Mode::Off), "Restoration waits for applied preference");
		feature.Frame();
		Require(feature.IsPerformanceCostMeasurementReady(), "Restoration reaches readiness");
	}
	for (bool unavailableByLoad : { false, true }) {
		feature.loaded = !unavailableByLoad;
		globals::instance.disabled = !unavailableByLoad;
		const auto status = ControlPointShadowCorrection({ { "action", "point_shadow_correction" } }).at("pointShadowCorrection");
		Require(status.at("requested") == "clip_only" && status.at("applied") == "clip_only" && status.at("effective") == "off" && status.at("pending") == false && status.at("available") == false, "Unavailable Clip is settled effective Off");
		Require(ControlPointShadowCorrection({ { "action", "point_shadow_correction" }, { "pointShadowCorrection", { { "mode", "off" } } } }).at("errorCode") == "feature_unavailable", "Unavailable mutation rejected");
		feature.SetPointShadowCorrection(Mode::Off);
		feature.Frame();
		Require(!feature.IsPointShadowCorrectionPending() && feature.GetAppliedPointShadowCorrection() == Mode::Off && feature.GetEffectivePointShadowCorrection() == Mode::Off,
			"Unavailable feature still reconciles the preference without activating correction");
		feature.SetPointShadowCorrection(Mode::ClipOnly);
		feature.Frame();
	}
	feature.loaded = true;
	globals::instance.disabled = false;
	feature.SetPerformanceToggleEnabled(false);
	feature.Frame();
	for (const auto name : { "off", "clip_only" }) {
		ControlPointShadowCorrection({ { "action", "point_shadow_correction" }, { "pointShadowCorrection", { { "mode", name } } } });
		Require(!feature.IsContactShadowsRequested() && !feature.settings.EnableParticleLights, "API mode changes leave other components off");
		Require(ControlPointShadowCorrection({ { "action", "point_shadow_correction" } }).at("pointShadowCorrection").at("requested") == name, "Query preserves current mode");
	}
	for (const json patch : { json(), json::object(), json{ { "mode", 1 } }, json{ { "mode", "bad" } }, json{ { "mode", "full" } }, json{ { "mode", "off" }, { "unknown", true } } }) {
		Reject([&] { ControlPointShadowCorrection({ { "action", "point_shadow_correction" }, { "pointShadowCorrection", patch } }); });
		Require(feature.GetRequestedPointShadowCorrection() == Mode::ClipOnly, "Malformed API payload preserves preference");
	}
	for (const auto name : { "off", "clip_only" }) {
		const auto reply = Dispatch({ { "pointShadowCorrection", { { "mode", name } } } });
		Require(reply.at("ok") == true && reply.at("result").at("pointShadowCorrection").at("requested") == name,
			"Valid service envelopes must reach the requested mode");
		feature.Frame();
		const auto status = Dispatch().at("result").at("pointShadowCorrection");
		Require(status.at("requested") == name && status.at("applied") == name && status.at("effective") == name && status.at("pending") == false,
			"Fresh commands observe each reconciled mode");
	}
	const auto unchanged = feature.CapturePerformanceSettingsState();
	for (const json invalid : {
			 json{ { "unknown", true } }, json{ { "deadlineMs", 1 } },
			 json{ { "pointShadowCorrection", { { "mode", false } } } },
			 json{ { "pointShadowCorrection", { { "mode", "invalid" } } } },
			 json{ { "pointShadowCorrection", { { "mode", "full" } } } },
			 json{ { "pointShadowCorrection", { { "mode", "off" }, { "extra", 1 } } } },
			 json{ { "action", "status" }, { "pointShadowCorrection", { { "mode", "off" } } } } }) {
		const auto error = Dispatch(invalid);
		Require(error.at("ok") == false && error.at("error").at("code") == "invalid_settings" && error.at("error").at("field") == "pointShadowCorrection",
			"Malformed requests return a structured settings error");
		Require(feature.CapturePerformanceSettingsState() == unchanged, "Rejected service requests are atomic");
	}
	for (bool measurement : { true, false }) {
		PerformanceTuningRenderer::active = measurement;
		globals::profilerInstance.state = measurement ? Profiler::CaptureSessionState::None : Profiler::CaptureSessionState::Running;
		const auto error = Dispatch({ { "pointShadowCorrection", { { "mode", "off" } } } });
		Require(error.at("ok") == false && error.at("error").at("code") == (measurement ? "measurement_busy" : "profiler_busy"),
			"API mutations respect existing measurement ownership");
		Require(Dispatch().at("ok") == true && feature.CapturePerformanceSettingsState() == unchanged,
			"Status queries remain available without disrupting capture");
	}
	globals::profilerInstance.state = Profiler::CaptureSessionState::None;
	feature.loaded = false;
	Require(Dispatch({ { "pointShadowCorrection", { { "mode", "off" } } } }).at("error").at("code") == "feature_unavailable",
		"Unavailable features reject mutations through the service");
	Require(Dispatch().at("result").at("pointShadowCorrection").at("effective") == "off", "Unavailable service queries retain effective Off");
	feature.loaded = true;
	const auto command = Request({ { "pointShadowCorrection", { { "mode", "off" } } } });
	Require(Foundation().Dispatch(command, BuildResult).at("ok") == true, "Initial command accepted");
	Dispatch({ { "pointShadowCorrection", { { "mode", "clip_only" } } } });
	Require(Foundation().Dispatch(command, BuildResult).at("result").at("idempotentReplay") == true && feature.GetRequestedPointShadowCorrection() == Mode::ClipOnly,
		"Replaying an old command must not overwrite a newer preference");
	json spec{ { "description", "" } };
	AddPointShadowCorrectionSchema(spec);
	Require(spec["inputSchema"]["properties"]["action"]["enum"] == json::array({ "point_shadow_correction" }), "Actual schema augmentation registers action");
	const auto& input = spec["inputSchema"]["properties"]["pointShadowCorrection"];
	Require(input.at("additionalProperties") == false && input.at("required") == json::array({ "mode" }), "Actual input schema is strict");
	const auto& properties = spec["outputSchema"]["properties"]["result"]["properties"]["pointShadowCorrection"]["properties"];
	Require(properties.size() == 5 && properties.at("requested") == input.at("properties").at("mode"), "Status schema covers distinct preference/publication states");
	Require(input.at("properties").at("mode").at("enum") == json::array({ "off", "clip_only" }), "Schema exposes exactly Off and Clip");
	feature.SetPointShadowCorrection(Mode::ClipOnly);
	feature.RestoreDefaultSettings();
	Require(feature.GetRequestedPointShadowCorrection() == Mode::Off, "Reset restores Off by default");
	std::cout << "Point-shadow production settings, publication, snapshots, defaults and DevBench contracts passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
