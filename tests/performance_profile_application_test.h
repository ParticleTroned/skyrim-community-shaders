#pragma once

#include "Menu/PerformanceTuningController.h"
#include "Utils/RuntimeToggle.h"
#include <algorithm>
#include <functional>
#include <map>

struct Feature
{
	std::string name;
	bool loaded = true, sceneOwned = false, rejectToggle = false, measurable = true, profileToggle = true, presetEnables = false;
	bool inMenu = true;
	const char* profileReadOnlyReason = nullptr;
	nlohmann::json settings{ { "enabled", true }, { "preset", "Quality" }, { "tint", 0.75 } };
	static inline std::vector<Feature*> features;
	explicit Feature(std::string a_name) : name(std::move(a_name)) {}
	virtual ~Feature() = default;
	static const auto& GetFeatureList() { return features; }
	std::string GetShortName() const { return name; }
	std::string GetDisplayName() const { return name; }
	std::string_view GetCategory() const { return "Lighting"; }
	bool SupportsPerformanceCostMeasurement() const { return measurable; }
	bool SupportsPerformanceProfileToggle() const { return profileToggle; }
	const char* GetPerformanceProfileReadOnlyReason() const { return profileReadOnlyReason; }
	bool IsHiddenFromUserView() const { return false; }
	bool IsInMenu() const { return inMenu; }
	bool IsPerformanceToggleEnabled() const { return settings["enabled"]; }
	void SetPerformanceToggleEnabled(bool a_enabled)
	{
		if (!rejectToggle)
			settings["enabled"] = a_enabled;
	}
	virtual nlohmann::json CapturePerformanceCostMeasurementState() const { return settings; }
	void RestorePerformanceToggleState(const nlohmann::json& a_settings) { settings = a_settings; }
	std::vector<std::string> GetPerformancePresetNames() const { return { "Performance", "Balanced", "Quality", "Hoshipa" }; }
	std::string GetPerformancePresetName() const { return settings["preset"]; }
	bool ApplyPerformancePreset(std::string_view a_name)
	{
		settings["preset"] = a_name;
		if (presetEnables)
			settings["enabled"] = true;
		return true;
	}
};

struct Upscaling : Feature
{
	enum class UpscaleMethod : uint32_t
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class UpscalingTransitionApplyDisposition
	{
		Rejected,
		Queued
	};
	enum class VRUpscalingTransitionOrigin
	{
		CSMenu
	};
	struct Desired
	{
		UpscaleMethod method = UpscaleMethod::kNONE;
		bool pending = false;
		bool HasPendingSettings() const { return pending; }
	} desired;
	struct Result
	{
		UpscalingTransitionApplyDisposition disposition;
	};
	nlohmann::json selections{ { "upscaleMethod", 3 }, { "upscaleMethodNoDLSS", 2 },
		{ "qualityMode", 2 }, { "dlssPreset", 1 }, { "fsr4RuntimeEnable", false } };
	UpscaleMethod currentMethod = UpscaleMethod::kDLSS;
	bool rejectTransition = true, dlssAvailable = true, renderScalePreference = true;
	const char* compatibilityBlock = nullptr;
	int transitions = 0;
	Upscaling() : Feature("Upscaling") {}
	const char* GetUpscalingSelectionBlockReason() const { return compatibilityBlock; }
	Desired GetPendingVRRenderScaleDesiredProfile() const { return desired; }
	UpscaleMethod GetUpscaleMethod() const { return currentMethod; }
	bool GetVRRenderScaleModePreference() const { return renderScalePreference; }
	nlohmann::json CapturePerformanceCostMeasurementState() const override { return selections; }
	static UpscaleMethod ResolvePerformanceCostMeasurementMethod(uint32_t a_primary, uint32_t a_fallback);
	Result ApplyCSMenuUpscalingTransition(UpscaleMethod a_method, bool a_renderScale, uint32_t a_quality,
		uint32_t a_preset, const char*, VRUpscalingTransitionOrigin, uint64_t, bool a_fsr4)
	{
		++transitions;
		if (rejectTransition)
			return { UpscalingTransitionApplyDisposition::Rejected };
		if (a_renderScale != renderScalePreference)
			throw std::runtime_error("a profile changed the independent render-scale preference");
		desired = { a_method, true };
		selections["qualityMode"] = a_quality;
		selections["dlssPreset"] = a_preset;
		selections["fsr4RuntimeEnable"] = a_fsr4;
		return { UpscalingTransitionApplyDisposition::Queued };
	}
};

namespace globals
{
	struct State
	{
		bool isMainMenuOpen = false, isLoadingMenuOpen = false, saving = false, pendingPostLoadRuntimeReset = false;
		bool IsSaveLoadSafeModeActive() const { return saving; }
		bool IsEngineSaveLoadActivityActive() const { return saving; }
		bool IsMainOrLoadingMenuOpen() const { return isMainMenuOpen || isLoadingMenuOpen; }
		std::map<std::string, bool> disabled;
		bool IsFeatureDisabled(const std::string& a_name) const
		{
			const auto found = disabled.find(a_name);
			return found != disabled.end() && found->second;
		}
	};
	struct Menu
	{
		void RequestSettingsDirtyCheck() {}
	};
	inline State stateStorage;
	inline State* state = &stateStorage;
	inline Menu menuStorage;
	inline Menu* menu = &menuStorage;
	namespace features
	{
		inline Upscaling upscaling;
	}
}

Upscaling::UpscaleMethod Upscaling::ResolvePerformanceCostMeasurementMethod(uint32_t a_primary, uint32_t a_fallback)
{
	return static_cast<UpscaleMethod>(a_primary == 3 && !globals::features::upscaling.dlssAvailable ? a_fallback : a_primary);
}

class PerformanceTuningRenderer
{
public:
	static inline bool busy = false;
	static inline std::unordered_map<std::string, std::optional<PerformanceTuningController::DisabledConfiguration>> configurations;
	static bool HasActiveMeasurements() { return busy; }
	static const char* GetFeatureSceneControlBlockReason(Feature* a_feature) { return a_feature->sceneOwned ? "scene owned" : nullptr; }
	static const char* GetFeatureControlBlockReason(Feature* a_feature) { return GetFeatureSceneControlBlockReason(a_feature); }
	static bool SetFeatureEnabled(Feature* a_feature, bool a_enabled)
	{
		return PerformanceTuningController::SetEnabled(*a_feature, a_enabled, configurations[a_feature->name]);
	}
	static bool ApplyFeatureSettingsTransaction(const std::vector<Feature*>& a_features, const std::function<bool()>& a_apply, bool a_replaceToggleHistory = false)
	{
		return PerformanceTuningController::ApplyTransaction(a_features, configurations, a_apply, a_replaceToggleHistory);
	}
};

namespace logger
{
	inline void warn(const char*) {}
}
namespace MenuUI
{
	struct ActionFeedback
	{
		bool error = false;
		std::string message;
	};
}
#include "performance_profile_actions_under_test.h"

void TestProfileApplication()
{
	using namespace MenuUI::PerformancePage;
	using nlohmann::json;
	const auto require = [](bool a_condition, const char* a_message) {
		if (!a_condition)
			throw std::runtime_error(a_message);
	};
	Feature grass("GrassCollision"), gi("ScreenSpaceGI"), shadows("ScreenSpaceShadows"),
		sky("Skylighting"), terrain("TerrainBlending"), wet("Wetterness");
	auto& upscaling = globals::features::upscaling;
	Feature::features = { &grass, &gi, &shadows, &sky, &terrain, &wet, &upscaling };
	const auto originalGrass = grass.settings;
	const auto originalSky = sky.settings;
	require(!Apply("Performance")["accepted"], "rejected upscaling rejects the profile");
	require(grass.settings == originalGrass && sky.settings == originalSky && PerformanceTuningRenderer::configurations.empty(),
		"a late rejection restores native settings and removes newly created toggle history");
	PerformanceTuningRenderer::SetFeatureEnabled(&grass, false);
	const auto history = PerformanceTuningRenderer::configurations.at(grass.name);
	require(!Apply("Quality")["accepted"], "late rejection also covers enabling a previously disabled feature");
	require(!grass.IsPerformanceToggleEnabled() && PerformanceTuningRenderer::configurations.at(grass.name)->enabledState == history->enabledState,
		"rollback preserves existing enabled-state history");
	require(PerformanceTuningRenderer::SetFeatureEnabled(&grass, true), "the prior reversible switch still works after rejection");
	const auto beforeBlocked = upscaling.transitions;
	upscaling.sceneOwned = true;
	json saved;
	Save(saved);
	require(!Apply("Balanced")["accepted"] && !Capture("Upscaling")["accepted"], "scene ownership blocks upscaling apply and capture");
	json after;
	Save(after);
	require(saved == after && upscaling.transitions == beforeBlocked, "scene rejection changes neither the user profile nor the runtime");
	upscaling.sceneOwned = false;
	upscaling.compatibilityBlock = "incompatible capture library";
	require(!Apply("Balanced")["accepted"], "native upscaling compatibility blocks profiles before feature mutation");
	upscaling.compatibilityBlock = nullptr;
	shadows.settings["enabled"] = false;
	shadows.rejectToggle = true;
	require(!Apply("Performance")["accepted"] && grass.settings == originalGrass && upscaling.transitions == beforeBlocked,
		"a rejected feature rolls back earlier features without queuing upscaling");
	shadows.rejectToggle = false;
	globals::state->disabled[grass.name] = true;
	require(!Capture(grass.name)["accepted"], "startup-disabled features cannot be appended");
	upscaling.rejectTransition = false;
	upscaling.dlssAvailable = false;
	upscaling.currentMethod = Upscaling::UpscaleMethod::kFSR;
	const auto methodsBefore = upscaling.selections;
	const auto applied = Apply("Balanced");
	require(applied["accepted"] && applied["skippedUnavailableFeatures"] == json::array({ grass.name }), "unavailable features are skipped and reported");
	require(upscaling.desired.method == Upscaling::UpscaleMethod::kFSR && upscaling.currentMethod == Upscaling::UpscaleMethod::kFSR &&
				upscaling.selections["upscaleMethod"] == methodsBefore["upscaleMethod"] && upscaling.selections["upscaleMethodNoDLSS"] == methodsBefore["upscaleMethodNoDLSS"],
		"hardware fallback queues through native admission without overwriting its preferences");
	require(sky.settings["preset"] == "Balanced" && sky.settings["tint"] == originalSky["tint"], "named presets preserve independent visual tuning");
	require(Capture("Upscaling")["accepted"], "pending selections can be appended without turning upscaling off");
	Save(after);
	const auto& choices = after["Performance Profiles"]["choices"]["upscaling"];
	require(choices.size() == 5 && choices["upscaleMethod"] == 2 && choices["upscaleMethodNoDLSS"] == 2,
		"capturing an AMD selection excludes inactive NVIDIA preferences and visual settings");
	after["Performance Profiles"]["choices"]["presets"]["Skylighting"] = "Unavailable preset";
	Load(after);
	require(Resolve("User defined") == Resolve("MyPerformance"), "the former profile action name remains a compatible alias");
	require(!Apply("MyPerformance")["accepted"], "feature-owned preset validation rejects unknown presets before mutation");
	Load(json::object());
	sky.settings["enabled"] = false;
	wet.sceneOwned = true;
	const auto savedUpscaling = upscaling.selections;
	const auto savedSky = sky.settings;
	const int transitionsBeforeCapture = upscaling.transitions;
	const auto batch = CaptureAll();
	require(batch["accepted"] && batch["skippedFeatures"].contains(grass.name) && batch["skippedFeatures"].contains(wet.name),
		"bulk save reports unavailable and controlled features");
	json bulkSaved;
	Save(bulkSaved);
	require(bulkSaved["Performance Profiles"]["choices"]["features"][sky.name] == false &&
				bulkSaved["Performance Profiles"]["choices"]["upscaling"].size() == 5,
		"bulk save includes Off switches and only permitted upscaling selections");
	require(sky.settings == savedSky && upscaling.selections == savedUpscaling && upscaling.transitions == transitionsBeforeCapture,
		"saving all choices never applies a preset or starts an upscaling transition");
	wet.sceneOwned = false;
	require(Capture(wet.name)["accepted"], "editable features can be saved independently");
	Save(bulkSaved);
	const auto savedWet = bulkSaved["Performance Profiles"]["choices"]["presets"][wet.name];
	wet.sceneOwned = true;
	wet.settings["preset"] = "Performance";
	require(CaptureAll()["accepted"], "other editable features still save when one is controlled");
	Save(bulkSaved);
	require(bulkSaved["Performance Profiles"]["choices"]["presets"][wet.name] == savedWet,
		"skipped feature choices remain intact");
	PerformanceTuningRenderer::busy = true;
	require(!CaptureAll()["accepted"], "bulk save cannot capture transient measurement settings");
	json blockedSaved;
	Save(blockedSaved);
	require(blockedSaved == bulkSaved, "blocked bulk save retains the entire profile");
	PerformanceTuningRenderer::busy = false;
	wet.sceneOwned = false;
	const auto beforeException = grass.settings;
	const auto configurationsBeforeException = PerformanceTuningRenderer::configurations.size();
	bool threw = false;
	try {
		PerformanceTuningRenderer::ApplyFeatureSettingsTransaction({ &grass }, [&]() -> bool {
			PerformanceTuningRenderer::SetFeatureEnabled(&grass, false);
			throw std::runtime_error("injected failure");
		});
	} catch (const std::runtime_error&) {
		threw = true;
	}
	require(threw && grass.settings == beforeException && PerformanceTuningRenderer::configurations.size() == configurationsBeforeException,
		"unexpected exceptions restore native state and toggle history before propagating");
	PerformanceTuningRenderer::busy = true;
	require(!Apply("Balanced")["accepted"] && !Capture("Wetterness")["accepted"], "active measurements block profile mutations");
	PerformanceTuningRenderer::busy = false;
	globals::state->saving = true;
	require(!Apply("Balanced")["accepted"] && !Capture("Wetterness")["accepted"], "save/load ownership blocks profile mutations");
	globals::state->saving = false;
	PerformanceTuningRenderer::SetFeatureEnabled(&grass, true);
	PerformanceTuningRenderer::SetFeatureEnabled(&sky, true);
	PerformanceTuningRenderer::SetFeatureEnabled(&grass, false);
	PerformanceTuningRenderer::SetFeatureEnabled(&sky, false);
	const auto savedGrass = grass.settings;
	const auto grassHistory = PerformanceTuningRenderer::configurations.at(grass.name)->enabledState;
	const auto skyHistory = PerformanceTuningRenderer::configurations.at(sky.name)->enabledState;
	require(!PerformanceTuningRenderer::ApplyFeatureSettingsTransaction({ &grass }, [&] {
		grass.settings["tint"] = 0.1;
		return false; }, true), "rejected replacements must report failure");
	require(grass.settings == savedGrass && PerformanceTuningRenderer::configurations.at(grass.name)->enabledState == grassHistory,
		"rejected replacements retain the feature's settings and toggle history");
	require(PerformanceTuningRenderer::ApplyFeatureSettingsTransaction({ &grass }, [&] {
		grass.settings["tint"] = 0.2;
		return true; }, true), "successful replacements must report success");
	require(!PerformanceTuningRenderer::configurations.contains(grass.name) &&
				PerformanceTuningRenderer::configurations.at(sky.name)->enabledState == skyHistory,
		"loading or restoring one feature discards only its own stale toggle history");
	require(PerformanceTuningRenderer::SetFeatureEnabled(&grass, true) && grass.settings["tint"] == 0.2,
		"enabling a replaced feature must retain the newly loaded settings");

	Load(json::object());
	globals::state->disabled.clear();
	grass.loaded = true;
	grass.rejectToggle = false;
	Feature utility("Screenshot"), unsupported("NoToggle"), water("UnifiedWater"), reverseZ("ReverseZ");
	utility.measurable = false;
	unsupported.profileToggle = false;
	water.profileReadOnlyReason = "Disable Unified Water in Settings > Startup, save and restart.";
	reverseZ.profileReadOnlyReason = "Change Reverse Z in its feature UI or Settings > Startup, save and restart.";
	reverseZ.inMenu = false;
	reverseZ.settings["enabled"] = false;
	Feature::features.push_back(&utility);
	Feature::features.push_back(&unsupported);
	Feature::features.push_back(&water);
	Feature::features.push_back(&reverseZ);
	const auto findIncluded = [&](const char* name) {
		const auto status = IncludedFeaturesStatus();
		for (const auto& feature : status["features"])
			if (feature["feature"] == name)
				return feature;
		throw std::runtime_error("included feature is missing");
	};
	require(findIncluded("UnifiedWater")["selected"] && !findIncluded("UnifiedWater")["editable"] &&
				!findIncluded("ReverseZ")["selected"] && !findIncluded("ReverseZ")["editable"],
		"restart-only tiles must remain visible and reflect their selected states without allowing edits");
	require(!SelectIncludedFeature("UnifiedWater", false)["accepted"] && !SelectIncludedFeature("ReverseZ", true)["accepted"] &&
				!Capture("UnifiedWater")["accepted"],
		"restart-only features must reject draft and capture mutations");
	reverseZ.settings["enabled"] = true;
	require(findIncluded("ReverseZ")["selected"], "Reverse Z must follow the user's startup choice without waiting for live activation");
	auto legacyProfile = PerformanceProfiles::UserProfile{}.Save();
	legacyProfile["choices"] = { { "features", { { "UnifiedWater", false }, { "ReverseZ", false }, { "Skylighting", true } } } };
	Load({ { "Performance Profiles", legacyProfile } });
	require(user.choices["features"].size() == 1 && user.choices["features"]["Skylighting"] == true,
		"loading legacy MyPerformance choices must discard restart-only switches while preserving editable choices");
	const auto beforeSelection = sky.settings;
	const auto beforeUtility = utility.settings;
	const auto beforeUpscaling = upscaling.selections;
	json profileBeforeSelection;
	Save(profileBeforeSelection);
	require(SelectIncludedFeature(sky.name, false)["accepted"] && SelectIncludedFeature(utility.name, false)["accepted"],
		"loaded rendering and utility Enabled controls must be selectable");
	require(!SelectIncludedFeature(unsupported.name, true)["accepted"], "features without Enabled controls must be excluded");
	require(!SelectIncludedFeature("Unknown", true)["accepted"], "unknown features must be rejected");
	json unchanged;
	Save(unchanged);
	require(unchanged == profileBeforeSelection && sky.settings == beforeSelection && utility.settings == beforeUtility,
		"tile selections must not mutate MyPerformance or live settings");
	require(SaveIncludedFeatures()["accepted"], "draft selections must save to MyPerformance");
	require(!user.choices["features"].contains("UnifiedWater") && !user.choices["features"].contains("ReverseZ") &&
				water.IsPerformanceToggleEnabled() && reverseZ.IsPerformanceToggleEnabled(),
		"saving Included Features must exclude restart-only controls and preserve their settings");
	require(user.choices["features"][sky.name] == false && user.choices["features"][utility.name] == false &&
				sky.settings == beforeSelection && utility.settings == beforeUtility && upscaling.selections == beforeUpscaling,
		"Apply to MyPerformance must only store explicit On/Off choices");
	require(RestoreIncludedFeatures()["accepted"] && includedFeatureChoices[sky.name] == sky.IsPerformanceToggleEnabled() &&
				user.choices["features"][sky.name] == false,
		"restore must update only the draft from current Enabled states");
	PerformanceTuningRenderer::busy = true;
	require(!RestoreIncludedFeatures()["accepted"] && !SaveIncludedFeatures()["accepted"], "measurement-owned temporary settings must not become preset choices");
	PerformanceTuningRenderer::busy = false;
	utility.loaded = false;
	const auto savedProfile = user.Save();
	require(!SaveIncludedFeatures()["accepted"] && user.Save() == savedProfile, "availability changes must reject the entire draft atomically");
	utility.loaded = true;
	globals::state->disabled[utility.name] = true;
	require(!SelectIncludedFeature(utility.name, true)["accepted"], "startup-disabled features must not be offered");
	globals::state->disabled.clear();
	sky.presetEnables = true;
	upscaling.rejectTransition = false;
	require(Apply("MyPerformance")["accepted"] && !sky.IsPerformanceToggleEnabled() && !utility.IsPerformanceToggleEnabled(),
		"only Apply MyPerformance changes live features, and explicit Off must override a named preset's Enabled value");
	require(SelectIncludedFeature("Upscaling", false)["accepted"] && SaveIncludedFeatures()["accepted"] &&
				user.choices["upscaling"]["upscaleMethod"] == 0 && !user.choices["features"].contains("Upscaling"),
		"upscaling Off must use its native selection boundary rather than a feature toggle");
	Feature::features.clear();
}
