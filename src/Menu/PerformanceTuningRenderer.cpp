#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"

namespace
{
	constexpr float kTuningDeltaThresholdMs = 0.099f;
	constexpr int kTuningSettleFrames = 20;
	constexpr int kTuningHighlightFrames = 240;
	constexpr double kFeatureCostMeasurementSeconds = 3.0;
	constexpr double kFeatureCostMeasurementMilliseconds = kFeatureCostMeasurementSeconds * 1000.0;
	constexpr float kFeatureCostDisplayEpsilonMs = 0.0005f;
	constexpr float kFramePacingDetectionEpsilonMs = 1.0f;

	constexpr std::array<std::string_view, 12> kPerformanceFeatureOrder = {
		"Upscaling",
		"ScreenSpaceShadows",
		"ScreenSpaceGI",
		"LightLimitFix",
		"Skylighting",
		"TerrainBlending",
		"TerrainShadows",
		"VolumetricLighting",
		"VolumetricShadows",
		"WetnessEffects",
		"SubsurfaceScattering",
		"GrassCollision"
	};

	struct FeatureHighlightDirection
	{
		int gpu = 0;
		int cpu = 0;
	};

	struct TuningHighlightState
	{
		ProfilingRenderer::PerformanceTimingSummary baseline;
		bool pendingComparison = false;
		int lastEditFrame = -10000;
		int measureAfterFrame = 0;
		int expireFrame = 0;
		int frameDirection = 0;
		int fpsDirection = 0;
		int gpuTotalDirection = 0;
		int cpuTotalDirection = 0;
		std::unordered_map<std::string, FeatureHighlightDirection> featureDirections;
	};

	struct FeatureCostSample
	{
		double sampledDurationMs = 0.0;
		double frameMsSum = 0.0;
		double profilerGpuMsSum = 0.0;
		double profilerCpuMsSum = 0.0;
		double frameSampleWeight = 0.0;
		double profilerGpuSampleWeight = 0.0;
		double profilerCpuSampleWeight = 0.0;
		int presentSyncedSamples = 0;
		double framePacedSampleWeight = 0.0;
		double framePacingEligibleSampleWeight = 0.0;
		uint64_t lastSampleId = 0;
	};

	struct FeatureCostDelta
	{
		float frameMs = 0.0f;
		float fpsDelta = 0.0f;
		float profilerGpuMs = 0.0f;
		float profilerCpuMs = 0.0f;
		bool hasFrame = false;
		bool hasFps = false;
		bool hasProfilerGpu = false;
		bool hasProfilerCpu = false;
		bool fpsPresentSynced = false;
		bool fpsFramePaced = false;
	};

	enum class FeatureCostMeasurementPhase
	{
		Idle,
		SettlingCurrent,
		MeasuringCurrent,
		SettlingTest,
		MeasuringTest,
		SettlingRestoredCurrent,
		MeasuringRestoredCurrent,
		Complete
	};

	enum class PerformanceUserDefaultsRestoreResult
	{
		Failed,
		Missing,
		Unchanged,
		Restored
	};

	struct FeatureCostMeasurementState
	{
		FeatureCostMeasurementPhase phase = FeatureCostMeasurementPhase::Idle;
		json originalState;
		bool testEnabled = false;
		bool testStateApplied = false;
		double phaseStartTime = 0.0;
		uint64_t settleStartSampleId = 0;
		FeatureCostSample currentSample;
		FeatureCostSample testSample;
		FeatureCostSample restoredCurrentSample;
		FeatureCostDelta delta;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static bool g_profilerStateCaptured = false;
	static bool g_profilerWasUserEnabled = false;
	static std::unordered_map<std::string, std::string> g_performanceDefaultsMessages;

	void CaptureProfilerStateForPerformanceTuning()
	{
		if (g_profilerStateCaptured || !globals::profiler)
			return;

		g_profilerWasUserEnabled = globals::profiler->IsUserEnabled();
		g_profilerStateCaptured = true;
	}

	void RestoreProfilerStateAfterPerformanceTuning()
	{
		if (!g_profilerStateCaptured)
			return;

		if (globals::profiler && !g_profilerWasUserEnabled)
			globals::profiler->SetUserEnabled(false);

		g_profilerStateCaptured = false;
		g_profilerWasUserEnabled = false;
	}

	int GetDirectionFromFrameTimeDelta(float deltaMs)
	{
		if (std::abs(deltaMs) <= kTuningDeltaThresholdMs)
			return 0;

		return deltaMs > 0.0f ? 1 : -1;
	}

	float CalculateFeatureCostDeltaMs(float currentMs, float inactiveMs)
	{
		// Positive values are added cost; negative values are savings against the inactive state.
		return currentMs - inactiveMs;
	}

	int GetDirectionFromFeatureCostFrameTimeDelta(float deltaMs)
	{
		if (std::abs(deltaMs) <= kFeatureCostDisplayEpsilonMs)
			return 0;

		return deltaMs > 0.0f ? 1 : -1;
	}

	int GetDirectionFromFeatureCostFpsDelta(float deltaFps)
	{
		if (std::abs(deltaFps) <= kFeatureCostDisplayEpsilonMs)
			return 0;

		return deltaFps > 0.0f ? -1 : 1;
	}

	bool IsFeatureCostMeasurementRunning(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::SettlingCurrent ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
		       state.phase == FeatureCostMeasurementPhase::SettlingTest ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringTest ||
		       state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent;
	}

	bool IsFeatureCostMeasurementActive(const FeatureCostMeasurementState& state)
	{
		return IsFeatureCostMeasurementRunning(state);
	}

	bool IsAnyFeatureCostMeasurementRunning()
	{
		for (const auto& [_, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementActive(state))
				return true;
		}

		return false;
	}

	float AverageOrZero(double sum, double weight)
	{
		return weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
	}

	float AverageFeatureCostCurrentWindows(double firstSum, double firstWeight, double secondSum, double secondWeight)
	{
		if (firstWeight <= 0.0 || secondWeight <= 0.0)
			return 0.0f;

		// Both windows cover the same three-second duration. Average their means
		// equally so a faster window cannot dominate merely by containing more frames.
		return (AverageOrZero(firstSum, firstWeight) + AverageOrZero(secondSum, secondWeight)) * 0.5f;
	}

	bool IsFeatureCostSampleFramePaced(const FeatureCostSample& sample)
	{
		return sample.framePacingEligibleSampleWeight > 0.0 &&
		       sample.framePacedSampleWeight * 2.0 >= sample.framePacingEligibleSampleWeight;
	}

	bool HasCompleteFeatureCostMetricCoverage(const FeatureCostSample& sample, double metricSampleWeight)
	{
		constexpr double kSampleWeightEpsilon = 1.0e-6;
		return sample.frameSampleWeight > 0.0 &&
		       metricSampleWeight + kSampleWeightEpsilon >= sample.frameSampleWeight;
	}

	bool IsPositiveFiniteTiming(float value)
	{
		return std::isfinite(value) && value > 0.0f;
	}

	bool TryGetDisplayTimingMs(bool hasProfilerTiming, float profilerTimingMs, float& value)
	{
		if (hasProfilerTiming && IsPositiveFiniteTiming(profilerTimingMs)) {
			value = profilerTimingMs;
			return true;
		}
		return false;
	}

	bool TryGetDisplayGpuMs(const ProfilingRenderer::PerformanceTimingSummary& summary, float& value)
	{
		return TryGetDisplayTimingMs(summary.hasProfilerGpu, summary.profilerGpuMs, value);
	}

	bool TryGetDisplayCpuMs(const ProfilingRenderer::PerformanceTimingSummary& summary, float& value)
	{
		return TryGetDisplayTimingMs(summary.hasProfilerCpu, summary.profilerCpuMs, value);
	}

	ProfilingRenderer::PerformanceTimingTotals GetTimingTotalsForFeature(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const std::string& shortName);
	std::vector<std::string> BuildProfilingPrefixesForFeature(const std::string& shortName);

	json MakeJsonMask(std::initializer_list<std::string_view> keys)
	{
		json mask = json::object();
		for (const auto key : keys) {
			mask[std::string(key)] = true;
		}
		return mask;
	}

	Feature* FindFeatureByShortName(std::string_view shortName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature)
				continue;

			const auto featureShortName = feature->GetShortName();
			if (std::string_view(featureShortName) == shortName)
				return feature;
		}

		return nullptr;
	}

	json GetPerformanceUserSettingsMask(Feature* feature, const json& currentSettings)
	{
		if (!feature)
			return json::object();

		const auto shortName = feature->GetShortName();
		if (shortName == "ScreenSpaceShadows") {
			return MakeJsonMask({ "Enable",
				"SampleCount" });
		}
		if (shortName == "ScreenSpaceGI") {
			return MakeJsonMask({ "Enabled",
				"EnableGI",
				"EnableExperimentalSpecularGI",
				"ResolutionMode",
				"NumSlices",
				"NumSteps",
				"EnableTemporalDenoiser",
				"EnableBlur" });
		}
		if (shortName == "TerrainBlending") {
			return MakeJsonMask({ "Enabled", "TerrainCullDistance" });
		}
		if (shortName == "SubsurfaceScattering") {
			return MakeJsonMask({ "EnableSubsurfaceScattering", "SSMode", "BurleySamples" });
		}
		if (shortName == "VolumetricLighting") {
			return MakeJsonMask({ "ExteriorEnabled",
				"ExteriorQuality",
				"ExteriorCustomSize",
				"InteriorEnabled",
				"InteriorQuality",
				"InteriorCustomSize",
				"DisableWeatherInteractionDuringRain" });
		}
		if (shortName == "WetnessEffects") {
			return MakeJsonMask({ "EnableWetnessEffects",
				"EnableRaindropFx",
				"RaindropFxRange",
				"EnableSplashes",
				"EnableRipples",
				"EnableVanillaRipples" });
		}

		const json mask = feature->CapturePerformanceSettingsState();
		if (!mask.is_object() && currentSettings.is_object())
			return currentSettings;

		return mask;
	}

	bool ReadUserSettingsJson(json& settings)
	{
		settings = json::object();

		const auto path = Util::PathHelpers::GetSettingsUserPath();
		std::ifstream input(path);
		if (!input.is_open())
			return true;

		try {
			input >> settings;
			if (!settings.is_object())
				settings = json::object();
			return true;
		} catch (const std::exception& e) {
			logger::warn("Failed to read performance tuning user defaults from {}: {}", path.string(), e.what());
			settings = json::object();
			return false;
		}
	}

	bool WriteUserSettingsJson(const json& settings)
	{
		const auto path = Util::PathHelpers::GetSettingsUserPath();
		try {
			std::filesystem::create_directories(path.parent_path());
		} catch (const std::exception& e) {
			logger::warn("Failed to create settings directory for {}: {}", path.string(), e.what());
			return false;
		}

		std::ofstream output(path);
		if (!output.is_open()) {
			logger::warn("Failed to open {} for performance tuning user defaults", path.string());
			return false;
		}

		try {
			output << settings.dump(1);
			return true;
		} catch (const std::exception& e) {
			logger::warn("Failed to write performance tuning user defaults to {}: {}", path.string(), e.what());
			return false;
		}
	}

	bool MergeJsonByMask(json& target, const json& source, const json& mask)
	{
		if (mask.is_object()) {
			if (!source.is_object())
				return false;
			if (!target.is_object())
				target = json::object();

			bool changed = false;
			for (const auto& [key, maskValue] : mask.items()) {
				if (!source.contains(key))
					continue;
				if (maskValue.is_object()) {
					changed |= MergeJsonByMask(target[key], source[key], maskValue);
				} else if (target[key] != source[key]) {
					target[key] = source[key];
					changed = true;
				}
			}
			return changed;
		}

		if (target == source)
			return false;

		target = source;
		return true;
	}

	bool SaveFeatureSettingsToUserDefaults(Feature* feature, json& userSettings, const json& mask)
	{
		if (!feature || !mask.is_object())
			return true;

		json currentSettings;
		feature->SaveSettings(currentSettings);
		json& savedFeatureSettings = userSettings[feature->GetName()];
		MergeJsonByMask(savedFeatureSettings, currentSettings, mask);
		return true;
	}

	bool SaveCrossFeaturePerformanceDefaults(Feature* feature, json& userSettings)
	{
		if (!feature)
			return false;

		if (feature->GetShortName() == "LightLimitFix" && globals::state) {
			const float refractionScale = std::isfinite(globals::state->refractionScale) ?
			                                  std::clamp(globals::state->refractionScale, 0.0f, 2.0f) :
			                                  1.0f;
			userSettings["Advanced"]["Refraction Scale"] = refractionScale;
		}
		return true;
	}

	bool SavePerformanceSettingsToUserDefaults(Feature* feature)
	{
		if (!feature)
			return false;

		json userSettings;
		if (!ReadUserSettingsJson(userSettings))
			return false;

		json currentSettings;
		feature->SaveSettings(currentSettings);
		const json mask = GetPerformanceUserSettingsMask(feature, currentSettings);
		if (!SaveFeatureSettingsToUserDefaults(feature, userSettings, mask))
			return false;
		if (!SaveCrossFeaturePerformanceDefaults(feature, userSettings))
			return false;

		if (!WriteUserSettingsJson(userSettings))
			return false;

		logger::info("Saved Performance Tuning user defaults for {}", feature->GetDisplayName());
		return true;
	}

	void RestoreFeatureSettingsFromUserDefaults(
		Feature* feature,
		const json& userSettings,
		const json& mask,
		bool& anyFound,
		bool& anyChanged,
		bool& anyFailed)
	{
		if (!feature || !mask.is_object())
			return;

		const auto featureName = feature->GetName();
		if (!userSettings.contains(featureName) || !userSettings[featureName].is_object())
			return;

		anyFound = true;

		json currentSettings;
		feature->SaveSettings(currentSettings);
		const json beforeSettings = currentSettings;
		if (!MergeJsonByMask(currentSettings, userSettings[featureName], mask) || currentSettings == beforeSettings)
			return;

		try {
			feature->LoadSettings(currentSettings);
			feature->ApplyPerformanceSettings();
			anyChanged = true;
		} catch (const std::exception& e) {
			logger::warn("Failed to restore Performance Tuning user defaults for {}: {}", feature->GetDisplayName(), e.what());
			anyFailed = true;
		} catch (...) {
			logger::warn("Failed to restore Performance Tuning user defaults for {}", feature->GetDisplayName());
			anyFailed = true;
		}
	}

	void RestoreCrossFeaturePerformanceDefaults(
		Feature* feature,
		const json& userSettings,
		bool& anyFound,
		bool& anyChanged,
		bool& anyFailed)
	{
		if (!feature || feature->GetShortName() != "LightLimitFix" || !globals::state)
			return;

		const auto advancedIt = userSettings.find("Advanced");
		if (advancedIt == userSettings.end() || !advancedIt->is_object())
			return;

		const auto refractionIt = advancedIt->find("Refraction Scale");
		if (refractionIt == advancedIt->end())
			return;

		anyFound = true;
		if (!refractionIt->is_number()) {
			anyFailed = true;
			return;
		}

		try {
			const float savedScale = refractionIt->get<float>();
			if (!std::isfinite(savedScale)) {
				anyFailed = true;
				return;
			}
			const float restoredScale = std::clamp(savedScale, 0.0f, 2.0f);
			if (globals::state->refractionScale != restoredScale) {
				globals::state->refractionScale = restoredScale;
				anyChanged = true;
			}
		} catch (const std::exception& e) {
			logger::warn("Failed to restore Performance Tuning heat-warp default: {}", e.what());
			anyFailed = true;
		}
	}

	json CapturePerformanceUiState(Feature* feature)
	{
		if (!feature)
			return json::object();

		json state = feature->CapturePerformanceSettingsState();
		if (!state.is_object())
			state = json{ { "FeatureState", std::move(state) } };

		if (feature->GetShortName() == "LightLimitFix" && globals::state) {
			state["PerformanceTuningGlobals"]["Refraction Scale"] =
				std::isfinite(globals::state->refractionScale) ? std::clamp(globals::state->refractionScale, 0.0f, 2.0f) : 1.0f;
		}
		return state;
	}

	PerformanceUserDefaultsRestoreResult RestorePerformanceSettingsFromUserDefaults(Feature* feature)
	{
		if (!feature)
			return PerformanceUserDefaultsRestoreResult::Failed;

		json userSettings;
		if (!ReadUserSettingsJson(userSettings))
			return PerformanceUserDefaultsRestoreResult::Failed;

		json currentSettings;
		feature->SaveSettings(currentSettings);
		const json mask = GetPerformanceUserSettingsMask(feature, currentSettings);
		bool anyFound = false;
		bool anyChanged = false;
		bool anyFailed = false;
		RestoreFeatureSettingsFromUserDefaults(feature, userSettings, mask, anyFound, anyChanged, anyFailed);
		RestoreCrossFeaturePerformanceDefaults(feature, userSettings, anyFound, anyChanged, anyFailed);

		if (anyFailed)
			return PerformanceUserDefaultsRestoreResult::Failed;
		if (!anyFound)
			return PerformanceUserDefaultsRestoreResult::Missing;
		if (!anyChanged)
			return PerformanceUserDefaultsRestoreResult::Unchanged;

		logger::info("Restored Performance Tuning user defaults for {}", feature->GetDisplayName());
		return PerformanceUserDefaultsRestoreResult::Restored;
	}

	bool AddFeatureCostSample(
		FeatureCostSample& sample,
		const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		if (!summary.valid)
			return false;

		if (summary.sampleId == 0 || summary.sampleId == sample.lastSampleId)
			return false;
		if (sample.lastSampleId != 0 && summary.sampleId < sample.lastSampleId)
			sample = {};
		sample.lastSampleId = summary.sampleId;

		if (!summary.hasFrameSample || !IsPositiveFiniteTiming(summary.frameSampleMs))
			return false;

		const double remainingDurationMs = kFeatureCostMeasurementMilliseconds - sample.sampledDurationMs;
		if (remainingDurationMs <= 0.0)
			return true;

		// Fractionally weight only the final boundary frame. This makes every phase
		// represent exactly three seconds of raw present intervals rather than a
		// UI-clock window rounded up by one frame.
		const double frameMs = static_cast<double>(summary.frameSampleMs);
		const double sampleWeight = std::min(1.0, remainingDurationMs / frameMs);
		sample.sampledDurationMs = std::min(
			kFeatureCostMeasurementMilliseconds,
			sample.sampledDurationMs + frameMs * sampleWeight);
		sample.frameMsSum += frameMs * sampleWeight;
		sample.frameSampleWeight += sampleWeight;
		if (summary.framePresentSynced)
			sample.presentSyncedSamples++;

		if (summary.hasProfilerGpuSample && summary.profilerGpuSampleMs > 0.0f &&
			summary.hasProfilerCpuSample && summary.profilerCpuSampleMs > 0.0f) {
			const float sceneWorkMs = std::max(summary.profilerGpuSampleMs, summary.profilerCpuSampleMs);
			sample.framePacingEligibleSampleWeight += sampleWeight;
			if (summary.frameSampleMs > sceneWorkMs + kFramePacingDetectionEpsilonMs)
				sample.framePacedSampleWeight += sampleWeight;
		}
		if (summary.hasProfilerGpuSample && summary.profilerGpuSampleMs > 0.0f) {
			sample.profilerGpuMsSum += static_cast<double>(summary.profilerGpuSampleMs) * sampleWeight;
			sample.profilerGpuSampleWeight += sampleWeight;
		}
		if (summary.hasProfilerCpuSample && summary.profilerCpuSampleMs > 0.0f) {
			sample.profilerCpuMsSum += static_cast<double>(summary.profilerCpuSampleMs) * sampleWeight;
			sample.profilerCpuSampleWeight += sampleWeight;
		}

		return sample.sampledDurationMs >= kFeatureCostMeasurementMilliseconds;
	}

	bool RenderUserDefaultsIconButton(
		const char* id,
		const char* fallbackLabel,
		ID3D11ShaderResourceView* texture,
		const ImVec2& imageSize)
	{
		if (texture) {
			auto iconButtonStyle = Util::TransparentIconButtonStyle();
			return Util::ImageButtonWithFlash(id, texture, imageSize);
		}

		return Util::ButtonWithFlash(fallbackLabel);
	}

	bool RenderPerformanceUserDefaultButtons(Feature* feature, bool disabled)
	{
		if (!feature || !globals::menu)
			return false;

		bool settingsRestored = false;
		const std::string featureKey = feature->GetShortName();
		auto& message = g_performanceDefaultsMessages[featureKey];
		auto& icons = globals::menu->uiIcons;
		const float iconSize = ImGui::GetFrameHeight();
		const ImVec2 imageSize(iconSize, iconSize);
		const std::string applyId = "##ApplyPerformanceDefaults" + featureKey;
		const std::string restoreId = "##RestorePerformanceDefaults" + featureKey;

		ImGui::Spacing();
		ImGui::BeginDisabled(disabled);
		if (RenderUserDefaultsIconButton(
				applyId.c_str(),
				"Apply settings to user defaults",
				icons.saveSettings.texture,
				imageSize)) {
			message = SavePerformanceSettingsToUserDefaults(feature) ?
			              "Performance user defaults updated." :
			              "Failed to update performance user defaults.";
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Apply the current Performance Tuning controls for this feature to user defaults.");
		}

		ImGui::SameLine();
		if (RenderUserDefaultsIconButton(
				restoreId.c_str(),
				"Reset to user defaults",
				icons.loadSettings.texture,
				imageSize)) {
			switch (RestorePerformanceSettingsFromUserDefaults(feature)) {
			case PerformanceUserDefaultsRestoreResult::Restored:
				settingsRestored = true;
				message = "Performance user defaults restored.";
				break;
			case PerformanceUserDefaultsRestoreResult::Unchanged:
				message = "Already using performance user defaults.";
				break;
			case PerformanceUserDefaultsRestoreResult::Missing:
				message = "No saved performance user defaults found.";
				break;
			case PerformanceUserDefaultsRestoreResult::Failed:
			default:
				message = "Failed to restore performance user defaults.";
				break;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reset the current Performance Tuning controls for this feature to saved user defaults.");
		}
		ImGui::EndDisabled();

		if (!message.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", message.c_str());
		}

		return settingsRestored;
	}

	void FinalizeFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		const float currentFrameMs = AverageFeatureCostCurrentWindows(
			state.currentSample.frameMsSum,
			state.currentSample.frameSampleWeight,
			state.restoredCurrentSample.frameMsSum,
			state.restoredCurrentSample.frameSampleWeight);
		const float currentProfilerGpuMs = AverageFeatureCostCurrentWindows(
			state.currentSample.profilerGpuMsSum,
			state.currentSample.profilerGpuSampleWeight,
			state.restoredCurrentSample.profilerGpuMsSum,
			state.restoredCurrentSample.profilerGpuSampleWeight);
		const float currentProfilerCpuMs = AverageFeatureCostCurrentWindows(
			state.currentSample.profilerCpuMsSum,
			state.currentSample.profilerCpuSampleWeight,
			state.restoredCurrentSample.profilerCpuMsSum,
			state.restoredCurrentSample.profilerCpuSampleWeight);
		const float inactiveFrameMs = AverageOrZero(state.testSample.frameMsSum, state.testSample.frameSampleWeight);
		const float inactiveProfilerGpuMs = AverageOrZero(state.testSample.profilerGpuMsSum, state.testSample.profilerGpuSampleWeight);
		const float inactiveProfilerCpuMs = AverageOrZero(state.testSample.profilerCpuMsSum, state.testSample.profilerCpuSampleWeight);
		const bool hasFrame =
			state.currentSample.frameSampleWeight > 0.0 &&
			state.restoredCurrentSample.frameSampleWeight > 0.0 &&
			state.testSample.frameSampleWeight > 0.0;
		const float currentFps = hasFrame ? 1000.0f / currentFrameMs : 0.0f;
		const float inactiveFps = hasFrame ? 1000.0f / inactiveFrameMs : 0.0f;

		state.delta.frameMs = CalculateFeatureCostDeltaMs(currentFrameMs, inactiveFrameMs);
		state.delta.fpsDelta = currentFps - inactiveFps;
		state.delta.profilerGpuMs = CalculateFeatureCostDeltaMs(currentProfilerGpuMs, inactiveProfilerGpuMs);
		state.delta.profilerCpuMs = CalculateFeatureCostDeltaMs(currentProfilerCpuMs, inactiveProfilerCpuMs);
		state.delta.hasFrame = hasFrame;
		state.delta.hasFps = hasFrame;
		state.delta.hasProfilerGpu =
			HasCompleteFeatureCostMetricCoverage(state.currentSample, state.currentSample.profilerGpuSampleWeight) &&
			HasCompleteFeatureCostMetricCoverage(state.restoredCurrentSample, state.restoredCurrentSample.profilerGpuSampleWeight) &&
			HasCompleteFeatureCostMetricCoverage(state.testSample, state.testSample.profilerGpuSampleWeight);
		state.delta.hasProfilerCpu =
			HasCompleteFeatureCostMetricCoverage(state.currentSample, state.currentSample.profilerCpuSampleWeight) &&
			HasCompleteFeatureCostMetricCoverage(state.restoredCurrentSample, state.restoredCurrentSample.profilerCpuSampleWeight) &&
			HasCompleteFeatureCostMetricCoverage(state.testSample, state.testSample.profilerCpuSampleWeight);
		state.delta.fpsPresentSynced =
			state.currentSample.presentSyncedSamples > 0 ||
			state.restoredCurrentSample.presentSyncedSamples > 0 ||
			state.testSample.presentSyncedSamples > 0;
		state.delta.fpsFramePaced =
			IsFeatureCostSampleFramePaced(state.currentSample) ||
			IsFeatureCostSampleFramePaced(state.restoredCurrentSample) ||
			IsFeatureCostSampleFramePaced(state.testSample);
	}

	void StartFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (!feature || !feature->SupportsPerformanceCostMeasurement() || !feature->IsPerformanceCostMeasurementEnabled())
			return;

		state = {};
		state.originalState = feature->CapturePerformanceCostMeasurementState();
		state.testEnabled = false;
		state.phase = FeatureCostMeasurementPhase::SettlingCurrent;
		state.phaseStartTime = currentTime;
		state.settleStartSampleId = globals::profiler ? globals::profiler->GetWholeFrameSampleId() : 0;
	}

	void ApplyFeatureCostMeasurementTestState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature)
			return;

		feature->SetPerformanceCostMeasurementEnabled(state.testEnabled);
		state.testStateApplied = true;
	}

	void BeginFeatureCostMeasurementTestSettle(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime,
		uint64_t currentSampleId)
	{
		ApplyFeatureCostMeasurementTestState(feature, state);
		state.testSample = {};
		state.phase = FeatureCostMeasurementPhase::SettlingTest;
		state.phaseStartTime = currentTime;
		state.settleStartSampleId = currentSampleId;
	}

	void RestoreFeatureCostMeasurementOriginalState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature || !state.testStateApplied)
			return;

		feature->RestorePerformanceCostMeasurementState(state.originalState);
		state.testStateApplied = false;
	}

	void BeginFeatureCostMeasurementRestoredCurrentSettle(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime,
		uint64_t currentSampleId)
	{
		RestoreFeatureCostMeasurementOriginalState(feature, state);
		state.restoredCurrentSample = {};
		state.phase = FeatureCostMeasurementPhase::SettlingRestoredCurrent;
		state.phaseStartTime = currentTime;
		state.settleStartSampleId = currentSampleId;
	}

	bool HasDrainedFeatureCostProfilerPipeline(
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current)
	{
		if (current.sampleId < state.settleStartSampleId) {
			// A profiler reset restarts sample IDs. Rebase and drain the new query ring.
			state.settleStartSampleId = current.sampleId;
			return false;
		}

		return current.sampleId - state.settleStartSampleId >= Profiler::kFrameLatency;
	}

	void BeginFeatureCostSampleWindow(
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		sample = {};
		// Do not reuse the result which completed the pipeline drain as the first
		// member of the new window. Its capture predates this phase boundary.
		sample.lastSampleId = current.sampleId;
		state.phase = phase;
		state.phaseStartTime = currentTime;
	}

	void UpdateFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		if (!feature || !IsFeatureCostMeasurementRunning(state))
			return;

		if (!feature->IsPerformanceCostMeasurementReady()) {
			if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
				state.phase == FeatureCostMeasurementPhase::SettlingCurrent) {
				state.currentSample = {};
				state.phase = FeatureCostMeasurementPhase::SettlingCurrent;
			} else if (state.phase == FeatureCostMeasurementPhase::MeasuringTest ||
				       state.phase == FeatureCostMeasurementPhase::SettlingTest) {
				state.testSample = {};
				state.phase = FeatureCostMeasurementPhase::SettlingTest;
			} else if (state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent ||
				           state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent) {
				state.restoredCurrentSample = {};
				state.phase = FeatureCostMeasurementPhase::SettlingRestoredCurrent;
			}

			state.phaseStartTime = currentTime;
			state.settleStartSampleId = current.sampleId;
			return;
		}

		const double elapsed = currentTime - state.phaseStartTime;

		if (state.phase == FeatureCostMeasurementPhase::SettlingCurrent) {
			if (HasDrainedFeatureCostProfilerPipeline(state, current)) {
				BeginFeatureCostSampleWindow(
					state.currentSample,
					FeatureCostMeasurementPhase::MeasuringCurrent,
					state,
					current,
					currentTime);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
			if (AddFeatureCostSample(state.currentSample, current)) {
				BeginFeatureCostMeasurementTestSettle(feature, state, currentTime, current.sampleId);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::SettlingTest) {
			const double settleSeconds = std::max(0.0, feature->GetPerformanceCostMeasurementSettleSeconds(state.testEnabled));
			if (elapsed >= settleSeconds && HasDrainedFeatureCostProfilerPipeline(state, current)) {
				BeginFeatureCostSampleWindow(
					state.testSample,
					FeatureCostMeasurementPhase::MeasuringTest,
					state,
					current,
					currentTime);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
			if (AddFeatureCostSample(state.testSample, current)) {
				BeginFeatureCostMeasurementRestoredCurrentSettle(feature, state, currentTime, current.sampleId);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent) {
			const double settleSeconds = std::max(0.0, feature->GetPerformanceCostMeasurementSettleSeconds(true));
			if (elapsed >= settleSeconds && HasDrainedFeatureCostProfilerPipeline(state, current)) {
				BeginFeatureCostSampleWindow(
					state.restoredCurrentSample,
					FeatureCostMeasurementPhase::MeasuringRestoredCurrent,
					state,
					current,
					currentTime);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent) {
			if (AddFeatureCostSample(state.restoredCurrentSample, current)) {
				FinalizeFeatureCostMeasurement(state);
				state.phase = FeatureCostMeasurementPhase::Complete;
			}
		}
	}

	void RenderDeltaMetric(const char* label, float value, int direction, const char* format)
	{
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine();
		if (direction != 0)
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::PerformanceDelta(direction));

		ImGui::Text(format, value);

		if (direction != 0)
			ImGui::PopStyleColor();
	}

	void RenderMetricCounter(const char* id, const char* label, float value, const char* format, int direction, bool valid, const char* tooltip)
	{
		ImGui::PushID(id);
		if (direction != 0)
			ImGui::PushStyleColor(ImGuiCol_Border, Util::Color::PerformanceDelta(direction));

		const float height = 58.0f * Util::GetUIScale();
		if (ImGui::BeginChild("##Counter", ImVec2(0.0f, height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::TextDisabled("%s", label);
			if (!valid) {
				ImGui::TextDisabled("--");
			} else if (direction != 0) {
				ImGui::TextColored(Util::Color::PerformanceDelta(direction), format, value);
			} else {
				ImGui::Text(format, value);
			}
		}
		ImGui::EndChild();
		if (tooltip) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("%s", tooltip);
		}

		if (direction != 0)
			ImGui::PopStyleColor();
		ImGui::PopID();
	}

	void RenderTopPerformanceCounters(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const TuningHighlightState& highlightState)
	{
		float displayGpuMs = 0.0f;
		const bool hasDisplayGpu = TryGetDisplayGpuMs(summary, displayGpuMs);
		float displayCpuMs = 0.0f;
		const bool hasDisplayCpu = TryGetDisplayCpuMs(summary, displayCpuMs);

		if (ImGui::BeginTable("##PerformanceTuningTopCounters", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			RenderMetricCounter("Total", "Total:", summary.frameMs, "%.2f ms", highlightState.frameDirection, summary.frameMs > 0.0f, "Present-to-present wall-clock frame time. It includes VSync, an FPS limit, and other time spent waiting in Present.");
			ImGui::TableNextColumn();
			RenderMetricCounter("GPU", "GPU:", displayGpuMs, "%.2f ms", highlightState.gpuTotalDirection, hasDisplayGpu, "D3D11 timestamp duration across the complete rendered frame. It excludes Present wait.");
			ImGui::TableNextColumn();
			RenderMetricCounter("CPU", "CPU:", displayCpuMs, "%.2f ms", highlightState.cpuTotalDirection, hasDisplayCpu, "CPU wall time from the frame boundary to just before Present. It excludes time spent in Present.");
			ImGui::TableNextColumn();
			RenderMetricCounter("FPS", "FPS:", summary.fps, "%.0f", highlightState.fpsDirection, summary.fps > 0.0f, "1000 divided by Total. This is the actual present rate, including frame pacing.");
			ImGui::EndTable();
		}
	}

	const char* GetFeatureCostComparisonLabel(Feature* feature, const FeatureCostMeasurementState& state)
	{
		(void)state;

		if (feature && feature->GetShortName() == "Upscaling")
			return "None";

		return "Off";
	}

	const char* GetFeatureCostComparisonDetails(Feature* feature)
	{
		if (!feature)
			return "the feature is switched off.";

		const std::string shortName = feature->GetShortName();
		if (shortName == "Upscaling")
			return "Upscaling is set to None.";
		if (shortName == "ScreenSpaceShadows")
			return "Screen Space Shadows are switched off.";
		if (shortName == "ScreenSpaceGI")
			return "SSGI/AO is switched off.";
		if (shortName == "LightLimitFix")
			return "particle lights, point-light contact shadows, and particle contact shadows are switched off.";
		if (shortName == "Skylighting")
			return "the in-game Enable Skylighting toggle is switched off, so probe updates stop and ambient shading plus reflection occlusion fall back to the unoccluded path.";
		if (shortName == "TerrainBlending")
			return "Terrain Blending is switched off.";
		if (shortName == "TerrainShadows")
			return "Terrain Shadows are switched off.";
		if (shortName == "VolumetricLighting")
			return "Volumetric Lighting is switched off for the current interior/exterior context.";
		if (shortName == "VolumetricShadows")
			return "Volumetric Shadows are switched off, so shadow-map downsampling and blur passes stop.";
		if (shortName == "WetnessEffects")
			return "the Wetness master toggle is switched off; active wetness, raindrop, splash, and custom ripple work stops, while the installed shader path and vanilla-ripple policy remain the same in both windows.";
		if (shortName == "SubsurfaceScattering")
			return "Subsurface Scattering is switched off.";
		if (shortName == "GrassCollision")
			return "Grass Collision is switched off.";

		return "the feature's measurement state is switched off.";
	}

	void RenderFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state)
	{
		if (!feature)
			return;

		const bool hasMeasurementState =
			IsFeatureCostMeasurementActive(state) ||
			state.phase == FeatureCostMeasurementPhase::Complete;
		if (!feature->SupportsPerformanceCostMeasurement() && !hasMeasurementState) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::TextDisabled("Actual feature cost is unavailable.");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				const char* reason = feature->GetPerformanceCostMeasurementUnavailableReason();
				ImGui::TextWrapped("%s", reason ? reason : "This feature has no runtime inactive state to compare in the current scene.");
			}
			return;
		}
		if (!feature->IsPerformanceCostMeasurementEnabled() && !hasMeasurementState)
			return;

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const bool running = IsFeatureCostMeasurementRunning(state);
		const bool anyMeasurementRunning = IsAnyFeatureCostMeasurementRunning();
		const bool canStartMeasurement =
			feature->IsPerformanceCostMeasurementEnabled() &&
			!running &&
			!anyMeasurementRunning;
		ImGui::BeginDisabled(!canStartMeasurement);
		if (ImGui::Button("Actual feature cost")) {
			StartFeatureCostMeasurement(feature, state, ImGui::GetTime());
		}
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped(
				"Measures current settings for %.0f seconds, measures the settled comparison state for %.0f seconds, restores the original settings, then measures the settled current state once more.",
				kFeatureCostMeasurementSeconds,
				kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped("Each window contains exactly %.0f seconds of raw present intervals. The boundary frame is fractionally weighted, and the rolling 60-frame display averages are not used.", kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped("The delayed GPU query pipeline is drained before each window so frames from the previous state cannot enter the next one.");
			if (feature && feature->GetShortName() == "Skylighting") {
				ImGui::TextWrapped("For Skylighting, the comparison state is the in-game Enable Skylighting toggle set to Off, not lower Skylighting settings.");
			}
			ImGui::TextWrapped(
				"Comparison: %s - %s",
				GetFeatureCostComparisonLabel(feature, state),
				GetFeatureCostComparisonDetails(feature));
			ImGui::TextWrapped(
				"Results depend on the current scene. Measure features where their inputs are active, such as Grass Collision where grass is visible or Screen Space Shadows under relevant lighting.");
		}
		if (!running && anyMeasurementRunning && !IsFeatureCostMeasurementActive(state)) {
			ImGui::SameLine();
			ImGui::TextDisabled("Finish the current measurement first");
		}

		if (state.phase == FeatureCostMeasurementPhase::SettlingCurrent) {
			ImGui::SameLine();
			ImGui::TextDisabled("Preparing exact profiler window");
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::SettlingTest ||
			state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent) {
			const bool restoringCurrent = state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent;
			const double settleSeconds = std::max(0.0, feature->GetPerformanceCostMeasurementSettleSeconds(restoringCurrent));
			const double elapsed = std::clamp(ImGui::GetTime() - state.phaseStartTime, 0.0, settleSeconds);
			ImGui::SameLine();
			if (restoringCurrent) {
				ImGui::TextDisabled("Restoring current: %s %.1f / %.1fs", feature->GetPerformanceCostMeasurementWaitText(), elapsed, settleSeconds);
			} else {
				ImGui::TextDisabled("%s %.1f / %.1fs", feature->GetPerformanceCostMeasurementWaitText(), elapsed, settleSeconds);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
			state.phase == FeatureCostMeasurementPhase::MeasuringTest ||
			state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent) {
			if (!feature->IsPerformanceCostMeasurementReady()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", feature->GetPerformanceCostMeasurementWaitText());
				return;
			}

			const FeatureCostSample& activeSample =
				state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ? state.currentSample :
				state.phase == FeatureCostMeasurementPhase::MeasuringTest ? state.testSample :
				                                                               state.restoredCurrentSample;
			const double elapsed = std::clamp(
				activeSample.sampledDurationMs / 1000.0,
				0.0,
				kFeatureCostMeasurementSeconds);
			ImGui::SameLine();
			if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
				ImGui::TextDisabled("Measuring current (1/2) %.1f / %.1fs", elapsed, kFeatureCostMeasurementSeconds);
			} else if (state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
				ImGui::TextDisabled(
					"Measuring %s %.1f / %.1fs",
					GetFeatureCostComparisonLabel(feature, state),
					elapsed,
					kFeatureCostMeasurementSeconds);
			} else {
				ImGui::TextDisabled("Measuring current (2/2) %.1f / %.1fs", elapsed, kFeatureCostMeasurementSeconds);
			}
			return;
		}

		if (state.phase != FeatureCostMeasurementPhase::Complete)
			return;

		if (!state.delta.hasFrame && !state.delta.hasFps && !state.delta.hasProfilerGpu && !state.delta.hasProfilerCpu) {
			ImGui::SameLine();
			ImGui::TextDisabled("No profiler timing data");
			return;
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Current (two windows) - %s", GetFeatureCostComparisonLabel(feature, state));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Frametime uses current settings minus %s: positive added cost is magenta; negative savings are green.", GetFeatureCostComparisonLabel(feature, state));
			ImGui::TextWrapped("Total is present-to-present, so VSync or an FPS limit can hide a workload change there; use GPU and CPU to see uncapped scene cost.");
			ImGui::TextWrapped("FPS uses the natural current-minus-%s sign: a drop is magenta and a gain is green.", GetFeatureCostComparisonLabel(feature, state));
		}
		if (state.delta.hasFrame)
			RenderDeltaMetric(
				"Total",
				state.delta.frameMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.frameMs),
				"%+.3f ms");
		if (state.delta.hasProfilerGpu)
			RenderDeltaMetric(
				"GPU",
				state.delta.profilerGpuMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.profilerGpuMs),
				"%+.3f ms");
		if (state.delta.hasProfilerCpu)
			RenderDeltaMetric(
				"CPU",
				state.delta.profilerCpuMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.profilerCpuMs),
				"%+.3f ms");
		if (state.delta.hasFps)
			RenderDeltaMetric(
				"FPS:",
				state.delta.fpsDelta,
				GetDirectionFromFeatureCostFpsDelta(state.delta.fpsDelta),
				"%+.1f");
		if (state.delta.fpsPresentSynced) {
			ImGui::Spacing();
			Util::Text::WrappedWarning("FPS is VSync-synced. FPS differences only appear once the feature pushes frame work beyond the sync budget; use GPU and CPU deltas to see its cost before then.");
		} else if (state.delta.fpsFramePaced) {
			ImGui::Spacing();
			Util::Text::WrappedWarning("FPS appears frame-paced or externally limited. FPS differences may only appear once the frame-pacing budget is exceeded; use GPU and CPU deltas to see its cost before then.");
		}
	}

	int GetFeatureListDirection(const TuningHighlightState& state, const std::string& shortName)
	{
		auto it = state.featureDirections.find(shortName);
		if (it == state.featureDirections.end())
			return 0;

		if (it->second.gpu > 0 || it->second.cpu > 0)
			return 1;
		if (it->second.gpu < 0 || it->second.cpu < 0)
			return -1;
		return 0;
	}

	int GetFeatureOrder(Feature* feature)
	{
		if (!feature)
			return static_cast<int>(kPerformanceFeatureOrder.size());

		const std::string shortName = feature->GetShortName();
		for (size_t i = 0; i < kPerformanceFeatureOrder.size(); ++i) {
			if (kPerformanceFeatureOrder[i] == shortName)
				return static_cast<int>(i);
		}

		return static_cast<int>(kPerformanceFeatureOrder.size());
	}

	bool ShouldShowInPerformanceTuning(Feature* feature)
	{
		if (!feature)
			return false;
		const std::string shortName = feature->GetShortName();
		return std::ranges::find(kPerformanceFeatureOrder, std::string_view(shortName)) != kPerformanceFeatureOrder.end();
	}

	std::vector<Feature*> BuildPerformanceFeatureList()
	{
		std::vector<Feature*> features;
		const bool essentialsMode = globals::menu && globals::menu->IsPerformanceUiMode();
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature || !feature->loaded || feature->IsHiddenFromUserView() ||
				(essentialsMode && feature->IsHiddenInEssentialsMode()) ||
				!feature->IsInMenu() || !feature->HasPerformanceSettings() ||
				!ShouldShowInPerformanceTuning(feature))
				continue;

			features.push_back(feature);
		}

		std::ranges::sort(features, [](Feature* lhs, Feature* rhs) {
			const int lhsOrder = GetFeatureOrder(lhs);
			const int rhsOrder = GetFeatureOrder(rhs);
			if (lhsOrder != rhsOrder)
				return lhsOrder < rhsOrder;

			return lhs->GetDisplayName() < rhs->GetDisplayName();
		});

		return features;
	}

	std::vector<std::string> BuildPerformanceFeaturePrefixes(const std::vector<Feature*>& features)
	{
		std::vector<std::string> prefixes;
		prefixes.reserve(features.size());
		for (auto* feature : features) {
			if (feature) {
				prefixes.push_back(feature->GetShortName());
			}
		}
		return prefixes;
	}

	std::vector<std::string> BuildProfilingPrefixesForFeature(const std::string& shortName)
	{
		return { shortName };
	}

	ProfilingRenderer::PerformanceTimingTotals GetTimingTotalsForFeature(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const std::string& shortName)
	{
		ProfilingRenderer::PerformanceTimingTotals totals;
		const auto prefixes = BuildProfilingPrefixesForFeature(shortName);
		for (const auto& prefix : prefixes) {
			const auto it = summary.features.find(prefix);
			if (it == summary.features.end())
				continue;

			totals.gpuAvgMs += it->second.gpuAvgMs;
			totals.cpuAvgMs += it->second.cpuAvgMs;
			totals.hasGpu = totals.hasGpu || it->second.hasGpu;
			totals.hasCpu = totals.hasCpu || it->second.hasCpu;
		}

		return totals;
	}

	Feature* FindSelectedFeature(const std::vector<Feature*>& features, std::string& selectedShortName)
	{
		if (features.empty()) {
			selectedShortName.clear();
			return nullptr;
		}

		auto it = std::ranges::find_if(features, [&](Feature* feature) {
			return feature && feature->GetShortName() == selectedShortName;
		});
		if (it != features.end())
			return *it;

		selectedShortName = features.front()->GetShortName();
		return features.front();
	}

	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state, bool allowPendingRestore)
	{
		(void)allowPendingRestore;

		if (!IsFeatureCostMeasurementActive(state)) {
			return;
		}

		if (feature && state.testStateApplied) {
			RestoreFeatureCostMeasurementOriginalState(feature, state);
		}

		state = {};
	}

	void ClearCompletedFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		if (state.phase == FeatureCostMeasurementPhase::Complete)
			state = {};
	}

	void ClearHighlightDirections(TuningHighlightState& state)
	{
		state.frameDirection = 0;
		state.fpsDirection = 0;
		state.gpuTotalDirection = 0;
		state.cpuTotalDirection = 0;
		state.featureDirections.clear();
	}

	void RecomputeHighlightDirections(
		TuningHighlightState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		const std::vector<Feature*>& features)
	{
		ClearHighlightDirections(state);
		if (!state.baseline.valid || !current.valid)
			return;

		float baselineGpuMs = 0.0f;
		float currentGpuMs = 0.0f;
		if (TryGetDisplayGpuMs(state.baseline, baselineGpuMs) && TryGetDisplayGpuMs(current, currentGpuMs))
			state.gpuTotalDirection = GetDirectionFromFrameTimeDelta(currentGpuMs - baselineGpuMs);
		float baselineCpuMs = 0.0f;
		float currentCpuMs = 0.0f;
		if (TryGetDisplayCpuMs(state.baseline, baselineCpuMs) && TryGetDisplayCpuMs(current, currentCpuMs))
			state.cpuTotalDirection = GetDirectionFromFrameTimeDelta(currentCpuMs - baselineCpuMs);
		if (state.baseline.frameMs > 0.0f && current.frameMs > 0.0f) {
			state.frameDirection = GetDirectionFromFrameTimeDelta(current.frameMs - state.baseline.frameMs);
			state.fpsDirection = state.frameDirection;
		}

		for (auto* feature : features) {
			if (!feature)
				continue;

			const std::string shortName = feature->GetShortName();
			const auto baselineTotals = GetTimingTotalsForFeature(state.baseline, shortName);
			const auto currentTotals = GetTimingTotalsForFeature(current, shortName);

			FeatureHighlightDirection direction;
			if (baselineTotals.hasGpu || currentTotals.hasGpu)
				direction.gpu = GetDirectionFromFrameTimeDelta(currentTotals.gpuAvgMs - baselineTotals.gpuAvgMs);
			if (baselineTotals.hasCpu || currentTotals.hasCpu)
				direction.cpu = GetDirectionFromFrameTimeDelta(currentTotals.cpuAvgMs - baselineTotals.cpuAvgMs);

			if (direction.gpu != 0 || direction.cpu != 0)
				state.featureDirections[shortName] = direction;
		}
	}

	void RegisterSettingsEdit(TuningHighlightState& state, const ProfilingRenderer::PerformanceTimingSummary& timingBeforeEdit, int frameCount)
	{
		const bool startsNewEditSequence = frameCount - state.lastEditFrame > 1;
		if (startsNewEditSequence && timingBeforeEdit.valid)
			state.baseline = timingBeforeEdit;

		state.pendingComparison = timingBeforeEdit.valid || state.pendingComparison;
		state.lastEditFrame = frameCount;
		state.measureAfterFrame = frameCount + kTuningSettleFrames;
		state.expireFrame = frameCount + kTuningHighlightFrames;
		ClearHighlightDirections(state);
	}

	void UpdateHighlightState(
		TuningHighlightState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		const std::vector<Feature*>& features,
		int frameCount)
	{
		if (state.pendingComparison && frameCount >= state.measureAfterFrame) {
			RecomputeHighlightDirections(state, current, features);
			state.pendingComparison = false;
		}

		if (!state.pendingComparison && frameCount > state.expireFrame) {
			ClearHighlightDirections(state);
		}
	}

	ProfilingRenderer::PerformanceTimingHighlight BuildSelectedHighlight(const TuningHighlightState& state, const std::string& selectedShortName)
	{
		ProfilingRenderer::PerformanceTimingHighlight highlight;
		highlight.frameDirection = state.frameDirection;
		highlight.fpsDirection = state.fpsDirection;
		highlight.gpuTotalDirection = state.gpuTotalDirection;
		highlight.cpuTotalDirection = state.cpuTotalDirection;

		auto it = state.featureDirections.find(selectedShortName);
		if (it != state.featureDirections.end()) {
			highlight.featureGpuDirection = it->second.gpu;
			highlight.featureCpuDirection = it->second.cpu;
		}

		return highlight;
	}
}

void PerformanceTuningRenderer::Render()
{
	static std::string selectedShortName;
	static TuningHighlightState highlightState;

	CaptureProfilerStateForPerformanceTuning();

	const auto features = BuildPerformanceFeatureList();
	auto* selectedFeature = FindSelectedFeature(features, selectedShortName);
	if (!selectedFeature) {
		ImGui::TextDisabled("No loaded performance settings are available.");
		return;
	}

	const auto featurePrefixes = BuildPerformanceFeaturePrefixes(features);
	const auto timingBeforeSettings = ProfilingRenderer::CapturePerformanceTimingSummary(featurePrefixes, true);
	const int frameCount = ImGui::GetFrameCount();
	const double currentTime = ImGui::GetTime();
	UpdateHighlightState(highlightState, timingBeforeSettings, features, frameCount);
	for (auto* feature : features) {
		if (!feature)
			continue;

		UpdateFeatureCostMeasurement(feature, g_costMeasurementStates[feature->GetShortName()], timingBeforeSettings, currentTime);
	}
	const bool anyMeasurementRunning = IsAnyFeatureCostMeasurementRunning();

	const float selectorWidth = std::max(180.0f * Util::GetUIScale(), ImGui::GetContentRegionAvail().x * 0.18f);

	RenderTopPerformanceCounters(timingBeforeSettings, highlightState);
	ImGui::Spacing();

	if (ImGui::BeginTable("##PerformanceTuningLayout", 3,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("##PerformanceFeatureSelector", ImGuiTableColumnFlags_WidthFixed, selectorWidth);
		ImGui::TableSetupColumn("##PerformanceSettings", ImGuiTableColumnFlags_WidthStretch, 1.25f);
		ImGui::TableSetupColumn("##PerformanceProfile", ImGuiTableColumnFlags_WidthStretch, 1.0f);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild("##PerformanceFeatureSelectorChild", ImVec2(0, 0), false)) {
			ImGui::BeginDisabled(anyMeasurementRunning);
			for (auto* feature : features) {
				if (!feature)
					continue;

				const bool selected = feature->GetShortName() == selectedShortName;
				const int featureDirection = GetFeatureListDirection(highlightState, feature->GetShortName());
				if (featureDirection != 0)
					ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::PerformanceDelta(featureDirection));
				if (ImGui::Selectable(feature->GetDisplayName().c_str(), selected, ImGuiSelectableFlags_None)) {
					selectedShortName = feature->GetShortName();
					selectedFeature = feature;
				}
				if (featureDirection != 0)
					ImGui::PopStyleColor();
			}
			ImGui::EndDisabled();
			if (anyMeasurementRunning) {
				ImGui::Spacing();
				ImGui::TextDisabled("Feature selection is locked while a cost test is running.");
			}
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild("##PerformanceSettingsChild", ImVec2(0, 0), false)) {
			ImGui::SeparatorText(selectedFeature->GetDisplayName().c_str());
			Util::PerformanceFrameStyleWrapper performanceStyle(true);
			auto& selectedCostState = g_costMeasurementStates[selectedFeature->GetShortName()];
			const json settingsStateBefore = CapturePerformanceUiState(selectedFeature);
			ImGui::BeginDisabled(anyMeasurementRunning);
			ImGui::BeginGroup();
			selectedFeature->DrawPerformanceSettings(true);
			ImGui::EndGroup();
			ImGui::EndDisabled();
			RenderFeatureCostMeasurement(selectedFeature, selectedCostState);
			const bool settingsRestored = RenderPerformanceUserDefaultButtons(selectedFeature, IsAnyFeatureCostMeasurementRunning());
			const json settingsStateAfter = CapturePerformanceUiState(selectedFeature);
			const bool settingsEdited = settingsRestored || settingsStateBefore != settingsStateAfter;
			if (settingsEdited) {
				RegisterSettingsEdit(highlightState, timingBeforeSettings, frameCount);
				ClearCompletedFeatureCostMeasurement(selectedCostState);
			}
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(2);
		if (ImGui::BeginChild("##PerformanceProfileChild", ImVec2(0, 0), false)) {
			ImGui::SeparatorText("Profiling");
			const auto selectedHighlight = BuildSelectedHighlight(highlightState, selectedFeature->GetShortName());
			const auto profilingPrefixes = BuildProfilingPrefixesForFeature(selectedFeature->GetShortName());
			ProfilingRenderer::RenderFeaturePerformanceSummary(profilingPrefixes, &selectedHighlight);
		}
		ImGui::EndChild();

		ImGui::EndTable();
	}
}

void PerformanceTuningRenderer::CancelActiveMeasurements(bool includePending)
{
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (includePending)
			ClearCompletedFeatureCostMeasurement(state);

		if (!IsFeatureCostMeasurementRunning(state)) {
			continue;
		}

		CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state, includePending);
	}

	RestoreProfilerStateAfterPerformanceTuning();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return IsAnyFeatureCostMeasurementRunning();
}
