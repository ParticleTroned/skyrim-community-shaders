#include "FeatureControlDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/IBL.h"
#	include "Features/ScreenSpaceShadows.h"
#	include "Features/Skylighting.h"
#	include "Features/VolumetricShadows.h"
#	include "Features/Wetterness.h"
#	include "Globals.h"
#	include "State.h"
#	include "Utils/Game.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <array>
#	include <chrono>
#	include <cmath>
#	include <functional>
#	include <future>
#	include <memory>
#	include <stdexcept>
#	include <string>
#	include <unordered_map>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };
	std::unordered_map<std::string, json> g_performanceSnapshots;
	std::unordered_map<std::string, json> g_qualityProfileSnapshots;

	struct SkylightingQualityProfile
	{
		const char* Name;
		uint ProbeGridQuality;
		uint OcclusionUpdateInterval;
		uint ProbeUpdateInterval;
		uint StableSliceCount;
		float ProbeFieldSize;
		float MinSpecularVisibility;
	};

	struct ScreenSpaceShadowsQualityProfile
	{
		const char* Name;
		float VRBaseSamplesAtReference;
		float VRCullDistance;
	};

	struct WetternessQualityProfile
	{
		const char* Name;
		float RaindropFxRangeWorldUnits;
		float WetnessDistanceFadeRange;
		float RaindropGridSize;
		float RaindropInterval;
		float RaindropChance;
		float SplashesLifetime;
		float RippleLifetime;
	};

	constexpr std::array kSkylightingQualityProfiles{
		SkylightingQualityProfile{ "Performance", 0u, 8u, 16u, 8u, 10240.0f, 0.05f },
		SkylightingQualityProfile{ "Balanced", 1u, 6u, 13u, 11u, 12970.667f, 0.10f },
		SkylightingQualityProfile{ "Quality", 2u, 5u, 9u, 13u, 15701.333f, 0.10f },
	};

	constexpr std::array kScreenSpaceShadowsQualityProfiles{
		ScreenSpaceShadowsQualityProfile{ "Performance", 16.0f, 20480.0f },
		ScreenSpaceShadowsQualityProfile{ "Balanced", 30.0f, 20480.0f },
		ScreenSpaceShadowsQualityProfile{ "Quality", 44.0f, 0.0f },
	};

	constexpr std::array kWetternessQualityProfiles{
		WetternessQualityProfile{ "Performance", 700.0f, 5000.0f, 3.60f, 0.65f, 0.60f, 4.5f, 0.22f },
		WetternessQualityProfile{ "Balanced", 1000.0f, 7500.0f, 3.25f, 0.58f, 0.70f, 5.2f, 0.26f },
		WetternessQualityProfile{ "Quality", 1400.0f, 10000.0f, 3.00f, 0.50f, 0.80f, 6.0f, 0.30f },
	};
	constexpr float kWetternessMinimumFadeRange = 100.0f / Util::Units::GAME_UNIT_TO_M;

	bool NearlyEqual(float a_left, float a_right)
	{
		return std::abs(a_left - a_right) <= 0.001f;
	}

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		taskInterface->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildPackageRecord(const Feature& a_feature, const char* a_featureName)
	{
		return {
			{ "feature", a_featureName },
			{ "control", "packageEnabled" },
			{ "valueType", "boolean" },
			{ "requestedValue", a_feature.loaded },
			{ "effectiveValue", a_feature.loaded },
			{ "runtimeActive", a_feature.loaded },
			{ "mutability", "restart" },
			{ "settle", { { "kind", "processRestart" }, { "minimumFrames", 0 }, { "requiresMenuClose", false }, { "resetsHistory", true } } },
			{ "cacheImpact", "feature-set-and-shader-permutations" },
			{ "resourceImpact", "load-or-unload-package-resources-and-hooks" },
			{ "canRestoreInSession", false },
			{ "writable", false },
			{ "available", true },
			{ "unavailableReason", nullptr },
		};
	}

	json BuildIBLRecord()
	{
		const auto& feature = globals::features::ibl;
		const bool value = feature.settings.EnableIBL != 0;
		return {
			{ "feature", "ImageBasedLighting" },
			{ "control", "EnableIBL" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", feature.IsRuntimeEnabled() },
			{ "mutability", "live" },
			{ "settle", { { "kind", "frames" }, { "minimumFrames", 2 }, { "requiresMenuClose", false }, { "resetsHistory", false } } },
			{ "cacheImpact", "none" },
			{ "resourceImpact", "retained" },
			{ "canRestoreInSession", true },
			{ "writable", feature.loaded },
			{ "available", feature.loaded },
			{ "unavailableReason", feature.loaded ? json(nullptr) : json("feature package is not loaded") },
		};
	}

	json BuildVolumetricShadowsRecord()
	{
		const auto& feature = globals::features::volumetricShadows;
		const bool value = feature.settings.Enabled;
		const bool hasDirectionalShadows = globals::state && globals::state->HasDirectionalShadows();
		return {
			{ "feature", "VolumetricShadows" },
			{ "control", "Enabled" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", feature.loaded && value && hasDirectionalShadows },
			{ "mutability", "live" },
			{ "settle", { { "kind", "frames" }, { "minimumFrames", 2 }, { "requiresMenuClose", false }, { "resetsHistory", false } } },
			{ "cacheImpact", "none" },
			{ "resourceImpact", "retained-lazy" },
			{ "canRestoreInSession", true },
			{ "writable", feature.loaded },
			{ "available", feature.loaded },
			{ "unavailableReason", feature.loaded ? json(nullptr) : json("feature package is not loaded") },
		};
	}

	Feature* FindFeatureByShortName(const std::string& a_featureName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && a_featureName == feature->GetShortName())
				return feature;
		}
		return nullptr;
	}

	bool ResetsHistoryForPerformanceToggle(const std::string& a_featureName)
	{
		return a_featureName == "Skylighting" ||
		       a_featureName == "ScreenSpaceGI" ||
		       a_featureName == "Wetterness" ||
		       a_featureName == "WetnessEffects" ||
		       a_featureName == "Upscaling";
	}

	const char* GetPerformanceResourceImpact(const std::string& a_featureName)
	{
		if (a_featureName == "Upscaling")
			return "render-target-relatch-and-vendor-resources";
		if (a_featureName == "VolumetricLighting")
			return "recreate-volumetric-targets";
		if (a_featureName == "Skylighting")
			return "reset-probe-resources-and-history";
		if (a_featureName == "Wetterness" || a_featureName == "WetnessEffects")
			return "reset-temporal-weather-state";
		if (a_featureName == "VR")
			return "shared-multi-feature-vr-state";
		return "retained";
	}

	json BuildPerformanceRecord(Feature& a_feature)
	{
		const std::string featureName = a_feature.GetShortName();
		const bool supported = a_feature.SupportsPerformanceCostMeasurement();
		const bool available = a_feature.loaded && supported;
		const bool value = available && a_feature.IsPerformanceCostMeasurementEnabled();
		return {
			{ "feature", featureName },
			{ "displayName", a_feature.GetDisplayName() },
			{ "settingsSection", a_feature.GetName() },
			{ "control", "performanceActive" },
			{ "valueType", "boolean" },
			{ "requestedValue", value },
			{ "effectiveValue", value },
			{ "runtimeActive", value },
			{ "ready", available && a_feature.IsPerformanceCostMeasurementReady() },
			{ "waitText", a_feature.GetPerformanceCostMeasurementWaitText() },
			{ "mutability", "live-settle" },
			{ "settle",
				{
					{ "kind", "seconds-and-readiness" },
					{ "whenEnabledSeconds", a_feature.GetPerformanceCostMeasurementSettleSeconds(true) },
					{ "whenDisabledSeconds", a_feature.GetPerformanceCostMeasurementSettleSeconds(false) },
					{ "requiresMenuCloseWhenEnabled", a_feature.RequiresMenuCloseForPerformanceCostMeasurement(true) },
					{ "requiresMenuCloseWhenDisabled", a_feature.RequiresMenuCloseForPerformanceCostMeasurement(false) },
					{ "menuCloseWaitMs", a_feature.GetPerformanceCostMeasurementMenuCloseWaitMs() },
					{ "resetsHistory", ResetsHistoryForPerformanceToggle(featureName) },
				} },
			{ "cacheImpact", "loaded-shader-set-retained" },
			{ "resourceImpact", GetPerformanceResourceImpact(featureName) },
			{ "canRestoreInSession", true },
			{ "snapshotHeld", g_performanceSnapshots.contains(featureName) },
			{ "writable", available },
			{ "available", available },
			{ "unavailableReason", available ? json(nullptr) : json(a_feature.loaded ? "feature does not expose the Performance Tuning measurement contract" : "feature package is not loaded") },
		};
	}

	json BuildPerformanceControlList()
	{
		json controls = json::array();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->SupportsPerformanceCostMeasurement())
				controls.push_back(BuildPerformanceRecord(*feature));
		}
		return controls;
	}

	bool SupportsQualityProfiles(const std::string& a_featureName)
	{
		return a_featureName == "Skylighting" ||
		       a_featureName == "ScreenSpaceShadows" ||
		       a_featureName == "Wetterness";
	}

	template <class TProfile, size_t N, class TMatches>
	std::string FindQualityProfileName(const std::array<TProfile, N>& a_profiles, TMatches a_matches)
	{
		for (const auto& profile : a_profiles) {
			if (a_matches(profile))
				return profile.Name;
		}
		return "Custom";
	}

	std::string GetQualityProfileName(const std::string& a_featureName)
	{
		if (a_featureName == "Skylighting") {
			const auto& settings = globals::features::skylighting.settings;
			return FindQualityProfileName(kSkylightingQualityProfiles, [&](const auto& profile) {
				return settings.ProbeGridQuality == profile.ProbeGridQuality &&
				       settings.OcclusionUpdateInterval == profile.OcclusionUpdateInterval &&
				       settings.ProbeUpdateInterval == profile.ProbeUpdateInterval &&
				       settings.StableSliceCount == profile.StableSliceCount &&
				       NearlyEqual(settings.ProbeFieldSize, profile.ProbeFieldSize) &&
				       NearlyEqual(settings.MinSpecularVisibility, profile.MinSpecularVisibility) &&
				       settings.EnableReducedUpdateFrequency &&
				       settings.EnableIncrementalProbeUpdates &&
				       settings.EnableFastProbeSampling;
			});
		}
		if (a_featureName == "ScreenSpaceShadows") {
			const auto& settings = globals::features::screenSpaceShadows.bendSettings;
			return FindQualityProfileName(kScreenSpaceShadowsQualityProfiles, [&](const auto& profile) {
				return NearlyEqual(settings.VRBaseSamplesAtReference, profile.VRBaseSamplesAtReference) &&
				       NearlyEqual(settings.VRCullDistance, profile.VRCullDistance);
			});
		}
		if (a_featureName == "Wetterness") {
			const auto& feature = globals::features::wetterness;
			const auto& settings = feature.settings;
			return FindQualityProfileName(kWetternessQualityProfiles, [&](const auto& profile) {
				return NearlyEqual(settings.RaindropFxRangeWorldUnits, profile.RaindropFxRangeWorldUnits) &&
				       NearlyEqual(feature.wetnessDistanceFadeRange, std::max(profile.WetnessDistanceFadeRange, kWetternessMinimumFadeRange)) &&
				       NearlyEqual(settings.RaindropGridSize, profile.RaindropGridSize) &&
				       NearlyEqual(settings.RaindropInterval, profile.RaindropInterval) &&
				       NearlyEqual(settings.RaindropChance, profile.RaindropChance) &&
				       NearlyEqual(settings.SplashesLifetime, profile.SplashesLifetime) &&
				       NearlyEqual(settings.RippleLifetime, profile.RippleLifetime);
			});
		}
		return "Unavailable";
	}

	json CaptureQualityProfileState(const std::string& a_featureName)
	{
		if (a_featureName == "Skylighting")
			return globals::features::skylighting.CapturePerformanceCostMeasurementState();
		if (a_featureName == "ScreenSpaceShadows")
			return globals::features::screenSpaceShadows.CapturePerformanceCostMeasurementState();
		if (a_featureName == "Wetterness")
			return globals::features::wetterness.CapturePerformanceCostMeasurementState();
		return json();
	}

	void RestoreQualityProfileState(const std::string& a_featureName, const json& a_state)
	{
		if (a_featureName == "Skylighting") {
			globals::features::skylighting.RestorePerformanceCostMeasurementState(a_state);
			return;
		}
		if (a_featureName == "ScreenSpaceShadows") {
			globals::features::screenSpaceShadows.RestorePerformanceCostMeasurementState(a_state);
			return;
		}
		if (a_featureName == "Wetterness") {
			globals::features::wetterness.RestorePerformanceCostMeasurementState(a_state);
			return;
		}
		throw std::runtime_error("feature does not expose quality profiles");
	}

	template <class TProfile, size_t N>
	const TProfile* FindQualityProfile(const std::array<TProfile, N>& a_profiles, const std::string& a_name)
	{
		for (const auto& profile : a_profiles) {
			if (a_name == profile.Name)
				return &profile;
		}
		return nullptr;
	}

	bool ApplyQualityProfile(const std::string& a_featureName, const std::string& a_profileName)
	{
		if (a_featureName == "Skylighting") {
			const auto* profile = FindQualityProfile(kSkylightingQualityProfiles, a_profileName);
			if (!profile)
				return false;
			auto state = globals::features::skylighting.CapturePerformanceCostMeasurementState();
			state["ProbeGridQuality"] = profile->ProbeGridQuality;
			state["OcclusionUpdateInterval"] = profile->OcclusionUpdateInterval;
			state["ProbeUpdateInterval"] = profile->ProbeUpdateInterval;
			state["StableSliceCount"] = profile->StableSliceCount;
			state["ProbeFieldSize"] = profile->ProbeFieldSize;
			state["MinSpecularVisibility"] = profile->MinSpecularVisibility;
			state["EnableReducedUpdateFrequency"] = true;
			state["EnableIncrementalProbeUpdates"] = true;
			state["EnableFastProbeSampling"] = true;
			globals::features::skylighting.RestorePerformanceCostMeasurementState(state);
			return true;
		}
		if (a_featureName == "ScreenSpaceShadows") {
			const auto* profile = FindQualityProfile(kScreenSpaceShadowsQualityProfiles, a_profileName);
			if (!profile)
				return false;
			auto state = globals::features::screenSpaceShadows.CapturePerformanceCostMeasurementState();
			state["Settings"]["VRBaseSamplesAtReference"] = profile->VRBaseSamplesAtReference;
			state["Settings"]["VRCullDistance"] = profile->VRCullDistance;
			globals::features::screenSpaceShadows.RestorePerformanceCostMeasurementState(state);
			return true;
		}
		if (a_featureName == "Wetterness") {
			const auto* profile = FindQualityProfile(kWetternessQualityProfiles, a_profileName);
			if (!profile)
				return false;
			auto state = globals::features::wetterness.CapturePerformanceCostMeasurementState();
			state["Settings"]["RaindropFxRangeWorldUnits"] = profile->RaindropFxRangeWorldUnits;
			state["WetnessDistanceFadeRange"] = profile->WetnessDistanceFadeRange;
			state["Settings"]["RaindropGridSize"] = profile->RaindropGridSize;
			state["Settings"]["RaindropInterval"] = profile->RaindropInterval;
			state["Settings"]["RaindropChance"] = profile->RaindropChance;
			state["Settings"]["SplashesLifetime"] = profile->SplashesLifetime;
			state["Settings"]["RippleLifetime"] = profile->RippleLifetime;
			globals::features::wetterness.RestorePerformanceCostMeasurementState(state);
			return true;
		}
		return false;
	}

	json BuildQualityProfileDefinitions(const std::string& a_featureName)
	{
		json definitions = json::array();
		if (a_featureName == "Skylighting") {
			for (const auto& profile : kSkylightingQualityProfiles) {
				definitions.push_back({
					{ "name", profile.Name },
					{ "parameters", {
						{ "ProbeGridQuality", profile.ProbeGridQuality },
						{ "OcclusionUpdateInterval", profile.OcclusionUpdateInterval },
						{ "ProbeUpdateInterval", profile.ProbeUpdateInterval },
						{ "StableSliceCount", profile.StableSliceCount },
						{ "ProbeFieldSize", profile.ProbeFieldSize },
						{ "MinSpecularVisibility", profile.MinSpecularVisibility },
					} },
				});
			}
		} else if (a_featureName == "ScreenSpaceShadows") {
			for (const auto& profile : kScreenSpaceShadowsQualityProfiles) {
				definitions.push_back({
					{ "name", profile.Name },
					{ "parameters", {
						{ "VRBaseSamplesAtReference", profile.VRBaseSamplesAtReference },
						{ "VRCullDistance", profile.VRCullDistance },
					} },
				});
			}
		} else if (a_featureName == "Wetterness") {
			for (const auto& profile : kWetternessQualityProfiles) {
				const float effectiveFadeRange = std::max(profile.WetnessDistanceFadeRange, kWetternessMinimumFadeRange);
				definitions.push_back({
					{ "name", profile.Name },
					{ "parameters", {
						{ "RaindropFxRangeWorldUnits", profile.RaindropFxRangeWorldUnits },
						{ "WetnessDistanceFadeRange", profile.WetnessDistanceFadeRange },
						{ "RaindropGridSize", profile.RaindropGridSize },
						{ "RaindropInterval", profile.RaindropInterval },
						{ "RaindropChance", profile.RaindropChance },
						{ "SplashesLifetime", profile.SplashesLifetime },
						{ "RippleLifetime", profile.RippleLifetime },
					} },
					{ "effectiveNormalization", {
						{ "WetnessDistanceFadeRange", effectiveFadeRange },
					} },
				});
			}
		}
		return definitions;
	}

	json BuildEffectiveQualityParameters(const std::string& a_featureName)
	{
		if (a_featureName == "Skylighting") {
			const auto& settings = globals::features::skylighting.settings;
			return {
				{ "ProbeGridQuality", settings.ProbeGridQuality },
				{ "OcclusionUpdateInterval", settings.OcclusionUpdateInterval },
				{ "ProbeUpdateInterval", settings.ProbeUpdateInterval },
				{ "StableSliceCount", settings.StableSliceCount },
				{ "ProbeFieldSize", settings.ProbeFieldSize },
				{ "MinSpecularVisibility", settings.MinSpecularVisibility },
			};
		}
		if (a_featureName == "ScreenSpaceShadows") {
			const auto& settings = globals::features::screenSpaceShadows.bendSettings;
			return {
				{ "VRBaseSamplesAtReference", settings.VRBaseSamplesAtReference },
				{ "VRCullDistance", settings.VRCullDistance },
				{ "compiledSamplesLeft", globals::features::screenSpaceShadows.compiledSampleCount },
				{ "compiledSamplesRight", globals::features::screenSpaceShadows.compiledSampleCountRight },
			};
		}
		if (a_featureName == "Wetterness") {
			const auto& feature = globals::features::wetterness;
			const auto& settings = feature.settings;
			return {
				{ "RaindropFxRangeWorldUnits", settings.RaindropFxRangeWorldUnits },
				{ "WetnessDistanceFadeRange", feature.wetnessDistanceFadeRange },
				{ "RaindropGridSize", settings.RaindropGridSize },
				{ "RaindropInterval", settings.RaindropInterval },
				{ "RaindropChance", settings.RaindropChance },
				{ "SplashesLifetime", settings.SplashesLifetime },
				{ "RippleLifetime", settings.RippleLifetime },
			};
		}
		return json::object();
	}

	json BuildQualityProfileRecord(const std::string& a_featureName)
	{
		auto* feature = FindFeatureByShortName(a_featureName);
		const bool supported = SupportsQualityProfiles(a_featureName);
		const bool available = feature && feature->loaded && supported;
		bool ready = available;
		const char* mutability = "live-settle";
		const char* cacheImpact = "loaded-shader-set-retained";
		const char* resourceImpact = "retained";
		double settleSeconds = 1.0;
		bool resetsHistory = false;
		if (a_featureName == "Skylighting") {
			ready = available && globals::features::skylighting.IsPerformanceCostMeasurementReady();
			resourceImpact = "recreate-probe-grid-and-reset-history";
			settleSeconds = 5.0;
			resetsHistory = true;
		} else if (a_featureName == "ScreenSpaceShadows") {
			auto& shadows = globals::features::screenSpaceShadows;
			const uint expectedSamples = available ? shadows.GetScaledSampleCount(false) : 0u;
			ready = available && (!shadows.IsPerformanceCostMeasurementEnabled() ||
			                     (shadows.compiledSampleCount == expectedSamples &&
			                         (shadows.useStereoReproject || shadows.compiledSampleCountRight == expectedSamples)));
			mutability = "live-recompile-settle";
			cacheImpact = "runtime-raymarch-shader-variant";
			resourceImpact = "release-and-recompile-per-eye-raymarch-shaders";
			settleSeconds = 2.0;
		} else if (a_featureName == "Wetterness") {
			resourceImpact = "reset-temporal-weather-state";
			settleSeconds = 5.0;
			resetsHistory = true;
		}

		return {
			{ "feature", a_featureName },
			{ "displayName", feature ? feature->GetDisplayName() : a_featureName },
			{ "control", "qualityProfile" },
			{ "valueType", "enum" },
			{ "allowedValues", json::array({ "Performance", "Balanced", "Quality" }) },
			{ "profileDefinitions", BuildQualityProfileDefinitions(a_featureName) },
			{ "effectiveParameters", available ? BuildEffectiveQualityParameters(a_featureName) : json::object() },
			{ "requestedValue", available ? json(GetQualityProfileName(a_featureName)) : json(nullptr) },
			{ "effectiveValue", available ? json(GetQualityProfileName(a_featureName)) : json(nullptr) },
			{ "runtimeActive", available && feature->IsPerformanceCostMeasurementEnabled() },
			{ "ready", ready },
			{ "mutability", mutability },
			{ "settle", { { "kind", "seconds-and-readiness" }, { "minimumSeconds", settleSeconds }, { "requiresMenuClose", false }, { "resetsHistory", resetsHistory } } },
			{ "cacheImpact", cacheImpact },
			{ "resourceImpact", resourceImpact },
			{ "canRestoreInSession", true },
			{ "snapshotHeld", g_qualityProfileSnapshots.contains(a_featureName) },
			{ "writable", available },
			{ "available", available },
			{ "unavailableReason", available ? json(nullptr) : json(feature ? "feature does not expose typed quality profiles" : "feature is not registered") },
		};
	}

	json BuildQualityParameterRecord()
	{
		auto record = BuildQualityProfileRecord("ScreenSpaceShadows");
		record["control"] = "qualityParameters";
		record["valueType"] = "object";
		record.erase("allowedValues");
		record.erase("profileDefinitions");
		record["parameterDefinitions"] = {
			{ "VRBaseSamplesAtReference", {
				{ "valueType", "number" },
				{ "minimum", 16.0f },
				{ "maximum", 96.0f },
				{ "effect", "changes the compiled per-eye ray-march sample count" },
			} },
			{ "VRCullDistance", {
				{ "valueType", "number" },
				{ "minimum", 0.0f },
				{ "maximum", 20480.0f },
				{ "zeroMeaning", "distance culling disabled" },
				{ "effect", "fades SSS over the final 20 percent, clamped to a 200-1200 unit fade band" },
			} },
		};
		record["requestedValue"] = record["effectiveParameters"];
		record["effectiveValue"] = record["effectiveParameters"];
		return record;
	}

	json BuildQualityProfileControlList()
	{
		return json::array({
			BuildQualityProfileRecord("Skylighting"),
			BuildQualityProfileRecord("ScreenSpaceShadows"),
			BuildQualityProfileRecord("Wetterness"),
		});
	}

	json BuildControlList()
	{
		json controls = json::array({
			BuildPackageRecord(globals::features::ibl, "ImageBasedLighting"),
			BuildIBLRecord(),
			BuildPackageRecord(globals::features::volumetricShadows, "VolumetricShadows"),
			BuildVolumetricShadowsRecord(),
		});
		for (auto& control : BuildPerformanceControlList())
			controls.push_back(std::move(control));
		for (auto& control : BuildQualityProfileControlList())
			controls.push_back(std::move(control));
		controls.push_back(BuildQualityParameterRecord());
		return controls;
	}

	json FindControl(const std::string& a_feature, const std::string& a_control)
	{
		if (a_feature == "ImageBasedLighting" && a_control == "packageEnabled")
			return BuildPackageRecord(globals::features::ibl, "ImageBasedLighting");
		if (a_feature == "ImageBasedLighting" && a_control == "EnableIBL")
			return BuildIBLRecord();
		if (a_feature == "VolumetricShadows" && a_control == "packageEnabled")
			return BuildPackageRecord(globals::features::volumetricShadows, "VolumetricShadows");
		if (a_feature == "VolumetricShadows" && a_control == "Enabled")
			return BuildVolumetricShadowsRecord();
		if (a_control == "performanceActive") {
			if (auto* feature = FindFeatureByShortName(a_feature))
				return BuildPerformanceRecord(*feature);
		}
		if (a_control == "qualityProfile" && SupportsQualityProfiles(a_feature))
			return BuildQualityProfileRecord(a_feature);
		if (a_feature == "ScreenSpaceShadows" && a_control == "qualityParameters")
			return BuildQualityParameterRecord();
		return {
			{ "error", "unknown control" },
			{ "feature", a_feature },
			{ "control", a_control },
		};
	}

	json SetControl(const std::string& a_feature, const std::string& a_control, bool a_value)
	{
		if (a_feature == "ImageBasedLighting" && a_control == "EnableIBL") {
			if (!globals::features::ibl.loaded)
				return { { "error", "feature package is not loaded" }, { "control", BuildIBLRecord() } };
			globals::features::ibl.settings.EnableIBL = a_value ? 1u : 0u;
			return { { "action", "set" }, { "persisted", false }, { "control", BuildIBLRecord() } };
		}
		if (a_feature == "VolumetricShadows" && a_control == "Enabled") {
			if (!globals::features::volumetricShadows.loaded)
				return { { "error", "feature package is not loaded" }, { "control", BuildVolumetricShadowsRecord() } };
			globals::features::volumetricShadows.settings.Enabled = a_value;
			return { { "action", "set" }, { "persisted", false }, { "control", BuildVolumetricShadowsRecord() } };
		}
		if (a_control == "performanceActive") {
			auto* feature = FindFeatureByShortName(a_feature);
			if (!feature)
				return FindControl(a_feature, a_control);
			if (!feature->loaded || !feature->SupportsPerformanceCostMeasurement())
				return { { "error", "performance control is unavailable" }, { "control", BuildPerformanceRecord(*feature) } };
			if (g_qualityProfileSnapshots.contains(a_feature))
				return { { "error", "restore the outstanding quality snapshot before changing performanceActive" }, { "control", BuildPerformanceRecord(*feature) } };

			bool restoredSnapshot = false;
			if (!a_value) {
				const bool inserted = !g_performanceSnapshots.contains(a_feature);
				if (inserted)
					g_performanceSnapshots.emplace(a_feature, feature->CapturePerformanceCostMeasurementState());
				try {
					feature->SetPerformanceCostMeasurementEnabled(false);
				} catch (...) {
					if (inserted)
						g_performanceSnapshots.erase(a_feature);
					throw;
				}
			} else if (const auto snapshot = g_performanceSnapshots.find(a_feature); snapshot != g_performanceSnapshots.end()) {
				feature->RestorePerformanceCostMeasurementState(snapshot->second);
				g_performanceSnapshots.erase(snapshot);
				restoredSnapshot = true;
			} else {
				feature->SetPerformanceCostMeasurementEnabled(true);
			}

			return {
				{ "action", "set" },
				{ "persisted", false },
				{ "restoredSnapshot", restoredSnapshot },
				{ "control", BuildPerformanceRecord(*feature) },
			};
		}
		if (a_control == "packageEnabled") {
			return {
				{ "error", "package control requires a process restart and is read-only in this session" },
				{ "control", FindControl(a_feature, a_control) },
			};
		}
		return FindControl(a_feature, a_control);
	}

	json SetQualityProfileControl(const std::string& a_feature, const std::string& a_profile)
	{
		if (!SupportsQualityProfiles(a_feature))
			return FindControl(a_feature, "qualityProfile");
		auto* feature = FindFeatureByShortName(a_feature);
		if (!feature || !feature->loaded)
			return { { "error", "quality profile control is unavailable" }, { "control", BuildQualityProfileRecord(a_feature) } };
		if (g_performanceSnapshots.contains(a_feature))
			return { { "error", "restore the outstanding performanceActive snapshot before changing qualityProfile" }, { "control", BuildQualityProfileRecord(a_feature) } };

		const bool inserted = !g_qualityProfileSnapshots.contains(a_feature);
		if (inserted)
			g_qualityProfileSnapshots.emplace(a_feature, CaptureQualityProfileState(a_feature));
		try {
			if (!ApplyQualityProfile(a_feature, a_profile)) {
				if (inserted)
					g_qualityProfileSnapshots.erase(a_feature);
				return {
					{ "error", "unknown quality profile" },
					{ "requestedValue", a_profile },
					{ "allowedValues", json::array({ "Performance", "Balanced", "Quality" }) },
					{ "control", BuildQualityProfileRecord(a_feature) },
				};
			}
		} catch (...) {
			if (inserted)
				g_qualityProfileSnapshots.erase(a_feature);
			throw;
		}

		return {
			{ "action", "set" },
			{ "persisted", false },
			{ "restoredSnapshot", false },
			{ "control", BuildQualityProfileRecord(a_feature) },
		};
	}

	json SetQualityParameterControl(const std::string& a_feature, const json& a_parameters)
	{
		if (a_feature != "ScreenSpaceShadows")
			return FindControl(a_feature, "qualityParameters");
		auto* feature = FindFeatureByShortName(a_feature);
		if (!feature || !feature->loaded)
			return { { "error", "quality parameter control is unavailable" }, { "control", BuildQualityParameterRecord() } };
		if (g_performanceSnapshots.contains(a_feature))
			return { { "error", "restore the outstanding performanceActive snapshot before changing qualityParameters" }, { "control", BuildQualityParameterRecord() } };
		if (a_parameters.empty())
			return { { "error", "qualityParameters requires at least one parameter" }, { "control", BuildQualityParameterRecord() } };

		for (auto parameter = a_parameters.begin(); parameter != a_parameters.end(); ++parameter) {
			if (parameter.key() != "VRBaseSamplesAtReference" && parameter.key() != "VRCullDistance") {
				return {
					{ "error", "unknown Screen Space Shadows quality parameter" },
					{ "parameter", parameter.key() },
					{ "control", BuildQualityParameterRecord() },
				};
			}
			if (!parameter.value().is_number()) {
				return {
					{ "error", "Screen Space Shadows quality parameters require numeric values" },
					{ "parameter", parameter.key() },
					{ "control", BuildQualityParameterRecord() },
				};
			}
			const double value = parameter.value().get<double>();
			const double minimum = parameter.key() == "VRBaseSamplesAtReference" ? 16.0 : 0.0;
			const double maximum = parameter.key() == "VRBaseSamplesAtReference" ? 96.0 : 20480.0;
			if (!std::isfinite(value) || value < minimum || value > maximum) {
				return {
					{ "error", "Screen Space Shadows quality parameter is outside its allowed range" },
					{ "parameter", parameter.key() },
					{ "requestedValue", parameter.value() },
					{ "minimum", minimum },
					{ "maximum", maximum },
					{ "control", BuildQualityParameterRecord() },
				};
			}
		}

		const bool inserted = !g_qualityProfileSnapshots.contains(a_feature);
		if (inserted)
			g_qualityProfileSnapshots.emplace(a_feature, CaptureQualityProfileState(a_feature));
		try {
			auto state = globals::features::screenSpaceShadows.CapturePerformanceCostMeasurementState();
			for (auto parameter = a_parameters.begin(); parameter != a_parameters.end(); ++parameter)
				state["Settings"][parameter.key()] = parameter.value();
			globals::features::screenSpaceShadows.RestorePerformanceCostMeasurementState(state);
		} catch (...) {
			if (inserted)
				g_qualityProfileSnapshots.erase(a_feature);
			throw;
		}

		return {
			{ "action", "set" },
			{ "persisted", false },
			{ "restoredSnapshot", false },
			{ "control", BuildQualityParameterRecord() },
		};
	}

	json RestoreQualityProfileSnapshot(const std::string& a_feature, const std::string& a_control)
	{
		auto buildControl = [&]() {
			return a_control == "qualityParameters" ? BuildQualityParameterRecord() : BuildQualityProfileRecord(a_feature);
		};
		const auto snapshot = g_qualityProfileSnapshots.find(a_feature);
		if (snapshot == g_qualityProfileSnapshots.end()) {
			return {
				{ "action", "restore" },
				{ "persisted", false },
				{ "restoredSnapshot", false },
				{ "control", buildControl() },
			};
		}
		RestoreQualityProfileState(a_feature, snapshot->second);
		g_qualityProfileSnapshots.erase(snapshot);
		return {
			{ "action", "restore" },
			{ "persisted", false },
			{ "restoredSnapshot", true },
			{ "control", buildControl() },
		};
	}

	json RestoreAllPerformanceSnapshots()
	{
		json restored = json::array();
		json failed = json::array();
		for (auto snapshot = g_performanceSnapshots.begin(); snapshot != g_performanceSnapshots.end();) {
			auto* feature = FindFeatureByShortName(snapshot->first);
			if (!feature) {
				failed.push_back({ { "feature", snapshot->first }, { "error", "feature is no longer registered" } });
				++snapshot;
				continue;
			}
			try {
				feature->RestorePerformanceCostMeasurementState(snapshot->second);
				restored.push_back(snapshot->first);
				snapshot = g_performanceSnapshots.erase(snapshot);
			} catch (const std::exception& e) {
				failed.push_back({ { "feature", snapshot->first }, { "error", e.what() } });
				++snapshot;
			} catch (...) {
				failed.push_back({ { "feature", snapshot->first }, { "error", "restore failed" } });
				++snapshot;
			}
		}
		for (auto snapshot = g_qualityProfileSnapshots.begin(); snapshot != g_qualityProfileSnapshots.end();) {
			try {
				RestoreQualityProfileState(snapshot->first, snapshot->second);
				restored.push_back(snapshot->first + ":qualityProfile");
				snapshot = g_qualityProfileSnapshots.erase(snapshot);
			} catch (const std::exception& e) {
				failed.push_back({ { "feature", snapshot->first }, { "control", "qualityProfile" }, { "error", e.what() } });
				++snapshot;
			} catch (...) {
				failed.push_back({ { "feature", snapshot->first }, { "control", "qualityProfile" }, { "error", "restore failed" } });
				++snapshot;
			}
		}
		return {
			{ "action", "restoreAll" },
			{ "persisted", false },
			{ "restored", std::move(restored) },
			{ "failed", std::move(failed) },
			{ "remainingPerformanceSnapshots", g_performanceSnapshots.size() },
			{ "remainingQualityProfileSnapshots", g_qualityProfileSnapshots.size() },
			{ "remainingSnapshots", g_performanceSnapshots.size() + g_qualityProfileSnapshots.size() },
		};
	}

	json BuildControlResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("list"));
		if (action == "list") {
			return RunOnMainThread([]() {
				return json{
					{ "action", "list" },
					{ "sessionOnlyMutations", true },
					{ "controls", BuildControlList() },
				};
			});
		}
		if (action == "restoreAll")
			return RunOnMainThread([]() { return RestoreAllPerformanceSnapshots(); });

		if (action != "get" && action != "set" && action != "restore") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "list", "get", "set", "restore", "restoreAll" }) },
			};
		}

		const auto feature = a_args.value("feature", std::string());
		const auto control = a_args.value("control", std::string());
		if (feature.empty() || control.empty())
			return { { "error", "feature and control are required" } };

		if (action == "get")
			return RunOnMainThread([feature, control]() { return json{ { "action", "get" }, { "control", FindControl(feature, control) } }; });
		if (action == "restore") {
			if (control != "qualityProfile" && control != "qualityParameters")
				return { { "error", "restore supports qualityProfile and qualityParameters; performanceActive restores through set value true" } };
			return RunOnMainThread([feature, control]() { return RestoreQualityProfileSnapshot(feature, control); });
		}

		const auto value = a_args.find("value");
		if (control == "qualityProfile") {
			if (value == a_args.end() || !value->is_string())
				return { { "error", "setting qualityProfile requires a string value" } };
			const auto requestedValue = value->get<std::string>();
			return RunOnMainThread([feature, requestedValue]() {
				return SetQualityProfileControl(feature, requestedValue);
			});
		}
		if (control == "qualityParameters") {
			if (value == a_args.end() || !value->is_object())
				return { { "error", "setting qualityParameters requires an object value" } };
			const auto requestedValue = *value;
			return RunOnMainThread([feature, requestedValue]() {
				return SetQualityParameterControl(feature, requestedValue);
			});
		}
		if (value == a_args.end() || !value->is_boolean())
			return { { "error", "setting this control requires a boolean value" } };
		const bool requestedValue = value->get<bool>();
		return RunOnMainThread([feature, control, requestedValue]() {
			return SetControl(feature, control, requestedValue);
		});
	}

	void RunHandler(
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = BuildControlResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}

	void ControlToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(a_argsJson, a_sink, a_write);
	}

	void ControlInspectExtensionHandler(
		void*,
		const char*,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		const json result{
			{ "registered", g_registered.load(std::memory_order_acquire) },
			{ "tool", "communityshaders.controls" },
			{ "usage", R"(Invoke communityshaders.controls directly, or dispatch it through a DevBench scenario tool step.)" },
			{ "actions", json::array({ "list", "get", "set", "restore", "restoreAll" }) },
		};
		const auto serialized = result.dump();
		a_write(a_sink, serialized.c_str());
	}

	DevBenchAPI::IDevBenchInterface001* GetDevBenchToolExtensionInterface()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;
		DevBenchAPI::DevBenchMessage message;
		messaging->Dispatch(
			DevBenchAPI::DevBenchMessage::kMessage_GetInterface,
			&message,
			sizeof(DevBenchAPI::DevBenchMessage*),
			DevBenchAPI::DevBenchPluginName);
		if (!message.GetApiFunction)
			return nullptr;
		return static_cast<DevBenchAPI::IDevBenchInterface001*>(
			message.GetApiFunction(kDevBenchToolExtensionRevision));
	}
}

namespace FeatureControlDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("FeatureControlDevBenchBridge: devbench host not present; controls tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Discover and mutate typed CSX feature controls with effective readback and explicit live/reload/restart metadata. Mutations are session-only and never rewrite arbitrary settings JSON. IBL and Volumetric Shadows expose audited inner toggles. Features that implement CSX's production Performance Tuning measurement contract also expose a reversible performanceActive control: the first disable snapshots the complete feature state, and enabling restores that snapshot. Skylighting, Screen Space Shadows, and Wetterness expose reversible Performance/Balanced/Quality profile clusters, including readiness, history reset, resource, and runtime shader-recompile metadata. Screen Space Shadows additionally exposes bounded qualityParameters so sample count and cull range can be calibrated independently. restore reverts one held quality snapshot; restoreAll is the session safety action for every outstanding snapshot. Package enablement remains restart-bound and read-only.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["list","get","set","restore","restoreAll"],"default":"list"},"feature":{"type":"string"},"control":{"type":"string","enum":["packageEnabled","EnableIBL","Enabled","performanceActive","qualityProfile","qualityParameters"]},"value":{"oneOf":[{"type":"boolean"},{"type":"string","enum":["Performance","Balanced","Quality"]},{"type":"object","properties":{"VRBaseSamplesAtReference":{"type":"number","minimum":16,"maximum":96},"VRCullDistance":{"type":"number","minimum":0,"maximum":20480}},"additionalProperties":false,"minProperties":1}]}},"required":["action"]}})";
		devBench->RegisterTool(
			"communityshaders.controls",
			descriptor,
			&ControlToolHandler,
			nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* inspectDescriptor =
				R"({"description":"Reports the CSX typed feature-control tool registration."})";
			if (auto* extensionDevBench = GetDevBenchToolExtensionInterface()) {
				extensionDevBench->RegisterToolExtension(
					"inspect",
					"communityshaders.controls",
					inspectDescriptor,
					&ControlInspectExtensionHandler,
					nullptr);
			}
		}
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"FeatureControlDevBenchBridge: registered communityshaders.controls with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace FeatureControlDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
