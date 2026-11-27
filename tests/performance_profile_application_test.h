#pragma once

#include "Menu/PerformanceTuningController.h"
#include "Presets/LookProfileState.h"
#include "Utils/RuntimeToggle.h"
#include <algorithm>
#include <functional>
#include <map>

struct Feature
{
	std::string name;
	bool loaded = true, sceneOwned = false, rejectToggle = false, measurable = true, profileToggle = true, presetEnables = false;
	bool inMenu = true;
	int loadFailures = 0, restoreFailures = 0, restores = 0, toggleFailures = 0;
	const char* profileReadOnlyReason = nullptr;
	nlohmann::json settings{ { "enabled", true }, { "preset", "Quality" }, { "tint", 0.75 } };
	nlohmann::json transient = nlohmann::json::object();
	static inline std::vector<Feature*> features;
	explicit Feature(std::string a_name) : name(std::move(a_name)) {}
	virtual ~Feature() = default;
	static const auto& GetFeatureList() { return features; }
	std::string GetShortName() const { return name; }
	std::string GetName() const { return name; }
	void SaveSettings(nlohmann::json& value)
	{
		value = settings;
		value.update(transient);
	}
	void SaveBaseSettings(nlohmann::json& value) { value = settings; }
	void LoadSettings(nlohmann::json& value)
	{
		settings = value;
		if (loadFailures > 0) {
			--loadFailures;
			throw std::runtime_error("Feature load failed");
		}
	}
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
		if (toggleFailures > 0) {
			--toggleFailures;
			throw std::runtime_error("Feature toggle failed");
		}
	}
	virtual nlohmann::json CapturePerformanceCostMeasurementState() const { return settings; }
	void RestorePerformanceToggleState(const nlohmann::json& a_settings)
	{
		++restores;
		if (restoreFailures > 0) {
			--restoreFailures;
			throw std::runtime_error("Native restoration failed");
		}
		settings = a_settings;
	}
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
		bool saveSucceeds = true;
		int saves = 0;
		nlohmann::json globalSettings{ { "Menu", { { "zoom", 1.25 } } }, { "General", { { "shaders", true } } }, { "Unavailable feature", { { "detail", 17 } } } };
		bool Save()
		{
			++saves;
			return saveSucceeds;
		}
		bool CaptureSettingsSnapshot(nlohmann::json& result, std::string&)
		{
			result = globalSettings;
			result["Disable at Boot"] = disabled;
			for (auto* feature : Feature::features)
				result[feature->name] = feature->settings;
			return true;
		}
		bool ApplySettingsSnapshot(const nlohmann::json& settings, std::string&)
		{
			if (settings.contains("Disable at Boot"))
				disabled = settings["Disable at Boot"].get<decltype(disabled)>();
			for (auto* feature : Feature::features)
				if (settings.contains(feature->name))
					feature->settings = settings[feature->name];
			for (const auto& [key, value] : settings.items())
				if (globalSettings.contains(key))
					globalSettings[key] = value;
			return true;
		}
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
		std::string selectedPage;
		int dirtyChecks = 0;
		void SelectFeatureMenu(const std::string& page) { selectedPage = page; }
		void RequestSettingsDirtyCheck() { ++dirtyChecks; }
		void OpenMenu() {}
		bool baselineAvailable = true;
		int preparations = 0;
		bool PrepareSettingsMutation()
		{
			++preparations;
			return baselineAvailable;
		}
		void CaptureFeatureMenuSettings(const std::string&, nlohmann::json&) {}
		void ApplyFeatureMenuSettings(const std::string&, const nlohmann::json*) {}
		std::string GetSettingsSaveMessage() const { return "Save failed"; }
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
	static nlohmann::json CaptureFeatureToggleHistory()
	{
		auto result = nlohmann::json::object();
		for (const auto& [name, state] : configurations)
			result[name] = state ? nlohmann::json{ { "enabled", state->enabledState }, { "disabled", state->disabledState } } : nlohmann::json(nullptr);
		return result;
	}
	static void RestoreFeatureToggleHistory(const nlohmann::json& history)
	{
		configurations.clear();
		for (const auto& [name, state] : history.items())
			configurations[name] = state.is_null() ? std::nullopt : std::optional(PerformanceTuningController::DisabledConfiguration{ state["enabled"], state["disabled"] });
	}
	static const char* GetFeatureSceneControlBlockReason(Feature* a_feature) { return a_feature->sceneOwned ? "scene owned" : nullptr; }
	static const char* GetFeatureControlBlockReason(Feature* a_feature) { return GetFeatureSceneControlBlockReason(a_feature); }
	static bool SetFeatureEnabled(Feature* a_feature, bool a_enabled);
	static const char* GetFeatureToggleFailureReason(Feature* feature);
	static bool ApplyFeatureSettingsTransaction(const std::vector<Feature*>& a_features, const std::function<bool()>& a_apply, bool a_replaceToggleHistory = false)
	{
		return PerformanceTuningController::ApplyTransaction(a_features, configurations, a_apply, a_replaceToggleHistory);
	}
};

inline auto& g_disabledFeatureConfigurations = PerformanceTuningRenderer::configurations;
inline const char* GetFeatureToggleBlockReason(Feature* feature) { return PerformanceTuningRenderer::GetFeatureControlBlockReason(feature); }
inline void InvalidateFeatureCostResults() {}
inline std::string g_featureToggleFailure;
inline Feature* failedToggleFeature = nullptr;
namespace logger
{
	template <class... Args>
	inline void warn(const char*, Args&&...)
	{}
}
#include "performance_feature_toggle_under_test.h"
inline bool PerformanceTuningRenderer::SetFeatureEnabled(Feature* feature, bool enabled)
{
	return SetRuntimeFeatureEnabled(feature, enabled);
}

namespace MenuUI
{
	inline const char* GetFeatureSettingsActionBlockReason(Feature* feature) { return PerformanceTuningRenderer::busy ? "busy" : feature ? nullptr :
		                                                                                                                                   "unavailable"; }
	struct ActionFeedback
	{
		bool error = false;
		std::string message;
	};
}
struct FeatureListRenderer
{
	static std::string GetBuiltInPageMenuEntryId(const std::string& page) { return "builtin:" + page; }
};
struct WeatherManager
{
	static WeatherManager* GetSingleton()
	{
		static WeatherManager manager;
		return &manager;
	}
	void NotifyUserSettingsChanged() {}
	void RefreshFeatureOverrides() {}
};
namespace WeatherVariables
{
	struct GlobalWeatherRegistry
	{
		static GlobalWeatherRegistry* GetSingleton()
		{
			static GlobalWeatherRegistry registry;
			return &registry;
		}
		void CaptureFeatureUserSettings(const std::string&) {}
	};
}
namespace Presets
{
	struct LookProfiles
	{
		static LookProfiles& Get()
		{
			static LookProfiles profiles;
			return profiles;
		}
		void SaveSelection(nlohmann::json&) {}
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
	json originalProfile, rejectedProfile;
	Save(originalProfile);
	globals::menu->baselineAvailable = false;
	for (const auto& action : std::vector<std::function<json()>>{
			 [] { return SetBase("Quality"); }, [] { return Capture("Skylighting"); },
			 [] { return CaptureAll(); }, [] { return CaptureCurrentSettings(); },
			 [] { return Apply("Quality"); }, [] { return BeginFeatureEdit("Skylighting"); } }) {
		const auto before = globals::menu->preparations;
		require(!action()["accepted"], "profile mutations must reject an unavailable saved baseline");
		require(globals::menu->preparations > before, "profile mutation must preserve the baseline before changing settings");
		Save(rejectedProfile);
		require(rejectedProfile == originalProfile && grass.settings == originalGrass && sky.settings == originalSky &&
					upscaling.transitions == 0 && globals::state->saves == 0,
			"baseline failure must preserve profile, live settings and pending transitions");
	}
	require(!PerformanceTuningRenderer::SetFeatureEnabled(&grass, false) && grass.settings == originalGrass &&
				PerformanceTuningRenderer::configurations.empty(),
		"direct feature toggles must preserve settings when the baseline is unavailable");
	require(failedToggleFeature == &grass && g_featureToggleFailure.find("saved settings") != std::string::npos,
		"baseline capture rejection must retain its actual diagnostic");
	require(SelectIncludedFeature(grass.name, false)["accepted"], "included-feature selection can be staged without changing settings");
	require(!SaveIncludedFeatures()["accepted"], "saving included features requires the saved baseline");
	Save(rejectedProfile);
	require(rejectedProfile == originalProfile && grass.settings == originalGrass, "failed inclusion saves preserve profile and live settings");
	globals::menu->baselineAvailable = true;
	for (int boundary = 0; boundary < 6; ++boundary) {
		const auto preparations = globals::menu->preparations;
		const auto checks = globals::menu->dirtyChecks;
		if (boundary == 0)
			globals::state = nullptr;
		if (boundary == 1)
			globals::stateStorage.saving = true;
		if (boundary == 2)
			globals::stateStorage.pendingPostLoadRuntimeReset = true;
		if (boundary == 3)
			globals::stateStorage.isLoadingMenuOpen = true;
		if (boundary == 4)
			globals::stateStorage.isMainMenuOpen = true;
		if (boundary == 5)
			PerformanceTuningRenderer::busy = true;
		require(!PerformanceTuningRenderer::SetFeatureEnabled(&grass, false) && grass.settings == originalGrass &&
					PerformanceTuningRenderer::configurations.empty() && globals::menu->preparations == preparations && globals::menu->dirtyChecks == checks,
			"unsafe direct toggles must reject before baseline capture, settings mutation or history changes");
		require(failedToggleFeature == &grass && !g_featureToggleFailure.empty(),
			"every blocked toggle must provide a diagnostic to UI and API callers");
		globals::state = &globals::stateStorage;
		globals::stateStorage.saving = globals::stateStorage.pendingPostLoadRuntimeReset = false;
		globals::stateStorage.isLoadingMenuOpen = globals::stateStorage.isMainMenuOpen = false;
		PerformanceTuningRenderer::busy = false;
	}
	grass.toggleFailures = 1;
	require(!PerformanceTuningRenderer::SetFeatureEnabled(&grass, false) && grass.settings == originalGrass &&
				PerformanceTuningRenderer::configurations.empty() && failedToggleFeature == &grass && !g_featureToggleFailure.empty(),
		"direct toggles must report exceptions without escaping the menu or losing the previous settings");
	require(PerformanceTuningRenderer::SetFeatureEnabled(&grass, true) && !failedToggleFeature && g_featureToggleFailure.empty(),
		"a successful no-op must clear stale toggle diagnostics");
	grass.toggleFailures = 1;
	const auto failedProfile = Apply("Performance");
	require(!failedProfile["accepted"] && failedProfile["error"].get<std::string>().find("Feature toggle failed") != std::string::npos &&
				grass.settings == originalGrass && sky.settings == originalSky && PerformanceTuningRenderer::configurations.empty() && upscaling.transitions == 0,
		"profiles must preserve the native toggle diagnostic and restore state before returning a failure");
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
	require(PerformanceTuningRenderer::SetFeatureEnabled(&sky, true), "prepare enabled configuration");
	sky.settings["preset"] = "Quality";
	require(PerformanceTuningRenderer::SetFeatureEnabled(&sky, false), "retain the old enabled configuration");
	sky.settings["preset"] = "Performance";
	PerformanceTuningRenderer::configurations.at(sky.name)->disabledState = sky.settings;
	user.choices["presets"][sky.name] = "Performance";
	user.choices["features"][sky.name] = true;
	const auto retainedHistory = PerformanceTuningRenderer::CaptureFeatureToggleHistory();
	const auto retainedSky = sky.settings;
	upscaling.rejectTransition = true;
	user.choices["upscaling"]["qualityMode"] = 6;
	require(!Apply("MyPerformance")["accepted"] && sky.settings == retainedSky &&
				PerformanceTuningRenderer::CaptureFeatureToggleHistory() == retainedHistory,
		"late rejection restores the superseded configuration and complete toggle history");
	upscaling.rejectTransition = false;
	require(Apply("MyPerformance")["accepted"] && sky.IsPerformanceToggleEnabled() && sky.settings["preset"] == "Performance",
		"enabling a saved preset must not restore a superseded configuration from toggle history");
	Load(json::object());
	globals::state->disabled.clear();
	const auto beforeBrokenRollback = grass.settings;
	const auto beforeBrokenSky = sky.settings;
	const auto checksBeforeBrokenRollback = globals::menu->dirtyChecks;
	grass.settings["enabled"] = false;
	PerformanceTuningRenderer::configurations.erase(grass.name);
	grass.restoreFailures = 1;
	upscaling.rejectTransition = true;
	user.choices["upscaling"]["qualityMode"] = 5;
	const auto brokenRollback = Apply("MyPerformance");
	require(!brokenRollback["accepted"] && brokenRollback["error"].get<std::string>().find("rollback failed") != std::string::npos &&
				brokenRollback["error"].get<std::string>().find("were restored") == std::string::npos && sky.settings == beforeBrokenSky &&
				globals::menu->dirtyChecks > checksBeforeBrokenRollback,
		"a failed native rollback must not claim complete restoration");
	grass.settings = beforeBrokenRollback;
	upscaling.rejectTransition = false;

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
	const auto savesBefore = globals::state->saves;
	require(BeginFeatureEdit(grass.name)["accepted"] && EditedFeature() == &grass, "double-click route opens the requested native editor");
	grass.settings["tint"] = 0.33;
	grass.transient["tint"] = 0.99;
	require(ApplyFeatureEdit()["accepted"] && user.settings[grass.name]["tint"] == 0.33 && globals::state->saves == savesBefore + 1,
		"Apply captures authored settings without baking transient weather values, and also persists normal settings");
	grass.transient.clear();
	require(EndFeatureEdit()["accepted"] && !EditedFeature() && grass.settings["tint"] == 0.33 &&
				globals::menu->selectedPage == "builtin:PerformancePresets",
		"Back keeps applied game settings and returns to tiles");
	require(BeginFeatureEdit(grass.name)["accepted"], "editor can reopen");
	grass.settings["tint"] = 0.91;
	const auto savedEditorProfile = user.Save();
	globals::state->saveSucceeds = false;
	require(!ApplyFeatureEdit()["accepted"] && user.Save() == savedEditorProfile && grass.settings["tint"] == 0.91,
		"failed normal save preserves the profile and live edits for retry");
	globals::state->saveSucceeds = true;
	grass.loadFailures = 1;
	require(!RestoreFeatureEdit()["accepted"] && grass.settings["tint"] == 0.91,
		"failed restoration rolls back complete feature values for retry");
	require(RestoreFeatureEdit()["accepted"] && grass.settings["tint"] == 0.33,
		"Restore uses this editor's opening settings, not defaults or the original game baseline");
	grass.settings["tint"] = 0.42;
	require(EndFeatureEdit()["accepted"] && grass.settings["tint"] == 0.42,
		"Back also retains unapplied live edits as requested");
	require(CaptureCurrentSettings()["accepted"] && user.settings[grass.name]["tint"] == 0.42 &&
				user.settings.contains("Menu") && user.settings.contains("General") && user.settings.contains("Unavailable feature") &&
				user.settings.contains(unsupported.name) && user.settings.contains(reverseZ.name),
		"copy ALL includes global, unsupported, restart-only and retained unavailable settings");
	const auto allSettingsProfile = user.Save();
	grass.settings["tint"] = 0.66;
	globals::state->globalSettings["Menu"]["zoom"] = 2;
	require(Apply("MyPerformance")["accepted"] && grass.settings["tint"] == 0.42 && globals::state->globalSettings["Menu"]["zoom"] == 1.25,
		"applying a full preset restores arbitrary feature tuning and global settings");
	sky.sceneOwned = true;
	grass.settings["tint"] = 0.88;
	require(Apply("MyPerformance")["accepted"] && grass.settings["tint"] == 0.42,
		"an unchanged scene-owned choice must not reject unrelated saved settings");
	const auto untouchedRestores = sky.restores;
	user.choices["upscaling"]["qualityMode"] = 4;
	upscaling.rejectTransition = true;
	const auto noOpRollback = Apply("MyPerformance");
	require(!noOpRollback["accepted"] && sky.restores == untouchedRestores,
		"late rejection must not restore or touch unchanged scene-owned controls");
	upscaling.rejectTransition = false;
	Load({ { "Performance Profiles", allSettingsProfile } });
	sky.sceneOwned = false;
	user.settings[sky.name]["preset"] = "Custom";
	user.choices["presets"][sky.name] = "Obsolete preset";
	require(Apply("MyPerformance")["accepted"] && sky.settings["preset"] == "Custom" && Status()["profiles"][3]["matches"],
		"custom settings supersede stale named presets in both application and status");
	sky.settings["enabled"] = !sky.IsPerformanceToggleEnabled();
	require(!Status()["profiles"][3]["matches"], "status still detects a real changed performance choice");
	sky.settings["enabled"] = !sky.IsPerformanceToggleEnabled();
	Load({ { "Performance Profiles", allSettingsProfile } });
	const auto beforeBlockedApply = grass.settings;
	user.choices["features"][sky.name] = !sky.IsPerformanceToggleEnabled();
	sky.sceneOwned = true;
	require(!Apply("MyPerformance")["accepted"] && grass.settings == beforeBlockedApply,
		"a real scene-owned change still rejects the profile and restores other settings");
	sky.sceneOwned = false;
	Load({ { "Performance Profiles", allSettingsProfile } });
	user.settings["Disable at Boot"][sky.name] = true;
	user.settings[sky.name]["enabled"] = true;
	user.choices["features"][sky.name] = false;
	const auto disabledByPreset = Apply("MyPerformance");
	require(disabledByPreset["accepted"] && globals::state->disabled[sky.name] && sky.IsPerformanceToggleEnabled() &&
				std::ranges::find(disabledByPreset["skippedUnavailableFeatures"], json(sky.name)) != disabledByPreset["skippedUnavailableFeatures"].end(),
		"snapshot startup switches must be honored before applying feature selections");
	Load({ { "Performance Profiles", allSettingsProfile } });
	user.settings[sky.name]["enabled"] = true;
	user.choices["features"][sky.name] = false;
	const auto enabledByPreset = Apply("MyPerformance");
	require(enabledByPreset["accepted"] && !globals::state->IsFeatureDisabled(sky.name) && !sky.IsPerformanceToggleEnabled() &&
				std::ranges::find(enabledByPreset["skippedUnavailableFeatures"], json(sky.name)) == enabledByPreset["skippedUnavailableFeatures"].end(),
		"loaded features made available by a snapshot must receive their explicit saved choices");
	Load({ { "Performance Profiles", allSettingsProfile } });
	PerformanceProfiles::UserProfile exchanged;
	require(PerformanceProfiles::ReadExport(PerformanceProfiles::MakeExport(user, "test"), exchanged) && exchanged.Save() == allSettingsProfile,
		"full settings survive profile export and import");
	globals::state->saveSucceeds = false;
	require(!CaptureCurrentSettings()["accepted"] && user.Save() == allSettingsProfile,
		"failed full capture save does not replace the prior profile");
	globals::state->saveSucceeds = true;
	Feature::features.clear();
}
