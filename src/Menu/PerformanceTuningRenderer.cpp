#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Feature.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/PerformanceTuningJson.h"
#include "Menu/PerformanceTuningMeasurement.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "menu.performance_tuning."

namespace
{
	constexpr float kTuningDeltaThresholdMs =
		static_cast<float>(PerformanceTuning::kPracticalFloorAbsoluteMs);
	constexpr uint64_t kTuningPostEditFreshSamples = 60;
	constexpr double kTuningPostEditSettleSeconds = 0.25;
	constexpr double kTuningPostEditTimeoutSeconds = 4.0;
	constexpr double kTuningHighlightSeconds = 4.0;
	constexpr double kFeatureCostMeasurementSeconds =
		PerformanceTuning::kMeasurementDurationMs / 1000.0;
	constexpr double kFeatureCostInitialWaitSeconds = 5.0;
	constexpr double kFeatureCostComparisonWaitSeconds = 5.0;
	constexpr double kFeatureCostCooldownSeconds = 7.0;
	constexpr double kFeatureCostSampleProgressTimeoutSeconds = 3.0;
	constexpr double kFeatureCostProfilerDrainTimeoutSeconds = 1.25;
	constexpr double kFeatureCostOverallTimeoutSeconds = 30.0;
	constexpr double kFeatureCostMeasurementMaximumSeconds =
		kFeatureCostMeasurementSeconds + 1.25;
	static_assert(
		kFeatureCostInitialWaitSeconds +
				2.0 * (kFeatureCostMeasurementMaximumSeconds +
						  kFeatureCostProfilerDrainTimeoutSeconds) +
				kFeatureCostComparisonWaitSeconds <
			kFeatureCostOverallTimeoutSeconds,
		"The bounded measurement protocol must finish within 30 seconds");
	static_assert(
		PerformanceTuning::kMaximumPresentIntervalMs ==
			Profiler::kMaxPresentIntervalMs,
		"Profiler and measurement interruption cutoffs must match");

	struct FeatureHighlightDirection
	{
		int gpu = 0;
		int cpu = 0;
	};

	struct TuningHighlightState
	{
		ProfilingRenderer::PerformanceTimingSummary baseline;
		bool pendingComparison = false;
		uint64_t lastEditSampleId = 0;
		uint64_t measureAfterSampleId = 0;
		double settleAfterTime = 0.0;
		double comparisonDeadline = 0.0;
		double expireTime = 0.0;
		int frameDirection = 0;
		int fpsDirection = 0;
		int gpuTotalDirection = 0;
		int cpuTotalDirection = 0;
		std::unordered_map<std::string, FeatureHighlightDirection> featureDirections;
	};

	enum class FeatureCostMeasurementLeg
	{
		Current,
		Comparison
	};

	enum class FeatureCostMeasurementStage
	{
		Idle,
		InitialDelay,
		Measuring,
		Draining,
		ComparisonDelay,
		Complete,
		Failed
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
		FeatureCostMeasurementStage stage = FeatureCostMeasurementStage::Idle;
		FeatureCostMeasurementLeg leg = FeatureCostMeasurementLeg::Current;
		json originalState;
		json resultSettingsState;
		bool featureStateRestorePending = false;
		bool autoVanityCameraSuppressionAcquired = false;
		bool menuClosedForMeasurement = false;
		bool frameGenerationConfigured = false;
		bool fpsLimitConfigured = false;
		bool restorePending = false;
		double overallStartTime = 0.0;
		double stageStartTime = 0.0;
		double lastProgressTime = 0.0;
		uint64_t lastObservedPresentSampleId = 0;
		uint64_t timingDiscontinuityEpoch = 0;
		uint64_t profilerSkippedCaptureStart = 0;
		PerformanceTuning::SampleWindow current;
		PerformanceTuning::SampleWindow comparison;
		PerformanceTuning::CostResult result;
		std::string failureReason;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static bool g_profilerStateCaptured = false;
	static bool g_profilerWasUserEnabled = false;
	static bool g_profilerCaptureLimitOwned = false;
	static Profiler::CaptureMode g_profilerPreviousCaptureLimit =
		Profiler::CaptureMode::DetailedPasses;
	static std::unordered_map<std::string, std::string> g_performanceDefaultsMessages;
	static std::unordered_map<std::string, json> g_lastPerformanceUiStates;
	static std::string g_selectedShortName;
	static TuningHighlightState g_highlightState;
	static ProfilingRenderer::PerformanceTimingSummary g_lastTimingSummary;
	static double g_featureCostCooldownUntilTime = 0.0;

	bool IsFeatureCostEnvironmentEligible()
	{
		// This runs from Present, after the render-scoped inWorld flag is cleared.
		const auto* player = globals::game::player;
		if (!globals::state || !player || !player->parentCell)
			return false;
		auto* ui = globals::game::ui;
		if (ui && (ui->GameIsPaused() ||
					  ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
					  ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
					  ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ||
					  ui->IsMenuOpen(RE::Console::MENU_NAME))) {
			return false;
		}

		if (globals::d3d::swapChain) {
			DXGI_SWAP_CHAIN_DESC description{};
			if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&description)) &&
				description.OutputWindow &&
				GetForegroundWindow() != description.OutputWindow) {
				return false;
			}
		}
		return true;
	}

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

	bool IsFeatureCostMeasurementRunning(const FeatureCostMeasurementState& state)
	{
		return state.stage == FeatureCostMeasurementStage::InitialDelay ||
		       state.stage == FeatureCostMeasurementStage::Measuring ||
		       state.stage == FeatureCostMeasurementStage::Draining ||
		       state.stage == FeatureCostMeasurementStage::ComparisonDelay;
	}

	bool IsAnyFeatureCostMeasurementRunning()
	{
		for (const auto& [_, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementRunning(state))
				return true;
		}
		return false;
	}

	bool IsFeatureCostMeasurementLocked(
		const FeatureCostMeasurementState& state)
	{
		return IsFeatureCostMeasurementRunning(state) || state.restorePending;
	}

	bool IsAnyFeatureCostMeasurementLocked()
	{
		for (const auto& [_, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementLocked(state))
				return true;
		}
		return false;
	}

	double GetFeatureCostCooldownRemaining(double currentTime)
	{
		return std::max(
			0.0,
			g_featureCostCooldownUntilTime - currentTime);
	}

	void SyncProfilerCaptureModeLimit()
	{
		if (!globals::profiler)
			return;

		if (IsAnyFeatureCostMeasurementRunning()) {
			if (!g_profilerCaptureLimitOwned) {
				g_profilerPreviousCaptureLimit =
					globals::profiler->GetCaptureModeLimit();
				g_profilerCaptureLimitOwned = true;
			}
			const auto tuningLimit =
				static_cast<uint8_t>(g_profilerPreviousCaptureLimit) <
						static_cast<uint8_t>(Profiler::CaptureMode::WholeFrameOnly) ?
					g_profilerPreviousCaptureLimit :
					Profiler::CaptureMode::WholeFrameOnly;
			globals::profiler->SetCaptureModeLimit(tuningLimit);
		} else if (g_profilerCaptureLimitOwned) {
			globals::profiler->SetCaptureModeLimit(
				g_profilerPreviousCaptureLimit);
			g_profilerCaptureLimitOwned = false;
			g_profilerPreviousCaptureLimit =
				Profiler::CaptureMode::DetailedPasses;
		}
	}

	PerformanceTuning::SampleWindow& GetFeatureCostSampleWindow(FeatureCostMeasurementState& state)
	{
		switch (state.leg) {
		case FeatureCostMeasurementLeg::Current:
			return state.current;
		case FeatureCostMeasurementLeg::Comparison:
		default:
			return state.comparison;
		}
	}

	const PerformanceTuning::SampleWindow& GetFeatureCostSampleWindow(
		const FeatureCostMeasurementState& state)
	{
		switch (state.leg) {
		case FeatureCostMeasurementLeg::Current:
			return state.current;
		case FeatureCostMeasurementLeg::Comparison:
		default:
			return state.comparison;
		}
	}

	const char* GetFeatureCostLegStatusLabel(const FeatureCostMeasurementState& state)
	{
		switch (state.leg) {
		case FeatureCostMeasurementLeg::Current:
			return T(TKEY("status.current"), "current");
		case FeatureCostMeasurementLeg::Comparison:
		default:
			return T(TKEY("status.comparison"), "comparison");
		}
	}

	std::string BuildFeatureCostProgressText(
		const FeatureCostMeasurementState& state,
		double currentTime)
	{
		switch (state.stage) {
		case FeatureCostMeasurementStage::InitialDelay:
			{
				const double remainingSeconds = std::max(
					0.0,
					kFeatureCostInitialWaitSeconds -
						(currentTime - state.stageStartTime));
				return std::vformat(
					T(TKEY("status.initial_wait"), "Starting in {:.1f} s"),
					std::make_format_args(remainingSeconds));
			}
		case FeatureCostMeasurementStage::ComparisonDelay:
			{
				const double remainingSeconds = std::max(
					0.0,
					kFeatureCostComparisonWaitSeconds -
						(currentTime - state.stageStartTime));
				return std::vformat(
					T(
						TKEY("status.comparison_wait_remaining"),
						"Comparison starts in {:.1f} s"),
					std::make_format_args(remainingSeconds));
			}
		case FeatureCostMeasurementStage::Measuring:
			{
				const char* legLabel = GetFeatureCostLegStatusLabel(state);
				const double remainingSeconds = std::max(
					0.0,
					kFeatureCostMeasurementSeconds -
						GetFeatureCostSampleWindow(state).sampledDurationMs /
							1000.0);
				return std::vformat(
					T(
						TKEY("status.measurement_remaining"),
						"{}: {:.1f} s remaining"),
					std::make_format_args(
						legLabel,
						remainingSeconds));
			}
		case FeatureCostMeasurementStage::Draining:
			{
				const char* legLabel = GetFeatureCostLegStatusLabel(state);
				const double remainingSeconds = std::max(
					0.0,
					kFeatureCostProfilerDrainTimeoutSeconds -
						(currentTime - state.stageStartTime));
				return std::vformat(
					T(
						TKEY("status.finalizing_remaining"),
						"Finalizing {}: {:.1f} s"),
					std::make_format_args(
						legLabel,
						remainingSeconds));
			}
		default:
			return {};
		}
	}

	Feature* FindFeatureByShortName(std::string_view shortName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && std::string_view(feature->GetShortName()) == shortName)
				return feature;
		}
		return nullptr;
	}

	bool IsFeatureControlledBySceneSettings(Feature* feature)
	{
		if (!feature)
			return false;

		auto* manager = globals::sceneSettingsManager;
		const auto shortName = feature->GetShortName();
		return manager &&
		       manager->HasActiveSettingsForFeature(shortName) &&
		       !manager->IsFeaturePaused(shortName);
	}

	json CapturePerformanceUiState(Feature* feature)
	{
		if (!feature)
			return json::object();

		return {
			{ "Feature", feature->CapturePerformanceSettingsState() },
			{ "Auxiliary", feature->CapturePerformanceTuningAuxiliaryState() }
		};
	}

	bool IsFeatureCostCadenceContextCurrent(
		const FeatureCostMeasurementState& state)
	{
		const auto& upscaling = globals::features::upscaling;
		const bool frameGenerationMatches =
			state.frameGenerationConfigured ==
			upscaling.IsFrameGenerationConfigured();
		const bool frameRateLimitMatches =
			state.fpsLimitConfigured ==
			upscaling.IsFrameRateLimitConfigured();
		return frameGenerationMatches && frameRateLimitMatches;
	}

	bool ReadSettingsJson(
		const std::filesystem::path& path,
		json& settings,
		bool allowMissing,
		std::string_view description)
	{
		settings = json::object();
		std::error_code fileError;
		const bool exists =
			std::filesystem::exists(path, fileError);
		if (fileError) {
			logger::warn(
				"Failed to inspect {} at {}: {}",
				description,
				path.string(),
				fileError.message());
			return false;
		}
		if (!exists)
			return allowMissing;

		std::ifstream input(path);
		if (!input.is_open()) {
			logger::warn(
				"Failed to open {} at {}",
				description,
				path.string());
			return false;
		}

		try {
			input >> settings;
			if (!settings.is_object())
				throw std::runtime_error("root value is not an object");
			return true;
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to read {} from {}: {}",
				description,
				path.string(),
				e.what());
			settings = json::object();
			return false;
		}
	}

	bool ReadUserSettingsJson(json& settings)
	{
		return ReadSettingsJson(
			Util::PathHelpers::GetSettingsUserPath(),
			settings,
			true,
			"Performance Tuning user defaults");
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
				} else if (!target.contains(key) || target[key] != source[key]) {
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

	bool HasJsonValuesForMask(const json& source, const json& mask)
	{
		if (!mask.is_object())
			return true;
		if (!source.is_object())
			return false;

		for (const auto& [key, maskValue] : mask.items()) {
			if (!source.contains(key))
				continue;
			if (!maskValue.is_object() ||
				!source[key].is_object() ||
				HasJsonValuesForMask(source[key], maskValue)) {
				return true;
			}
		}
		return false;
	}

	bool HasAllJsonValuesForMask(const json& source, const json& mask)
	{
		if (!mask.is_object())
			return true;
		if (!source.is_object())
			return false;

		for (const auto& [key, maskValue] : mask.items()) {
			const auto sourceIt = source.find(key);
			if (sourceIt == source.end())
				return false;
			if (maskValue.is_object() &&
				!HasAllJsonValuesForMask(*sourceIt, maskValue)) {
				return false;
			}
		}
		return true;
	}

	bool HasPerformanceDefaultsOverride(Feature* feature)
	{
		if (!feature)
			return false;

		auto* manager =
			SettingsOverrideManager::GetSingleton();
		if (!manager || !manager->IsEnabled())
			return false;
		const auto& overrides = manager->GetOverrides();
		// State applies Global.user whenever any valid override was discovered,
		// so its affected paths cannot safely be inferred through this API.
		if (!overrides.empty() && manager->HasUserOverride("Global"))
			return true;

		try {
			const std::string shortName = feature->GetShortName();
			const json performanceMask =
				feature->GetPerformanceTuningUserSettingsMask();
			// Feature .user files are applied as a separate high-priority layer and
			// this API does not expose their parsed paths. Treat their presence
			// conservatively, while filtering ordinary overrides precisely.
			if (manager->HasUserOverride(shortName))
				return true;
			for (const auto* overrideInfo :
				manager->GetFeatureOverrides(shortName)) {
				if (overrideInfo &&
					overrideInfo->enabled &&
					HasJsonValuesForMask(
						overrideInfo->overrideData,
						performanceMask)) {
					return true;
				}
			}

			json featureMask = json::object();
			featureMask[feature->GetName()] =
				performanceMask;
			const json auxiliaryMask =
				feature->CapturePerformanceTuningAuxiliaryState();

			for (const auto& overrideInfo : overrides) {
				if (!overrideInfo.isGlobal || !overrideInfo.enabled)
					continue;
				if (HasJsonValuesForMask(
						overrideInfo.overrideData,
						featureMask) ||
					HasJsonValuesForMask(
						overrideInfo.overrideData,
						auxiliaryMask)) {
					return true;
				}
			}
		} catch (...) {
			// A defaults action must not bypass an override merely because its
			// affected-path mask could not be captured.
			return true;
		}

		return false;
	}

	bool MaterializeAuxiliarySettings(
		json& materializedSettings,
		const json& userSettings,
		const json& defaultSettings,
		const json& auxiliaryMask)
	{
		materializedSettings = json::object();
		if (!auxiliaryMask.is_object())
			return false;
		if (auxiliaryMask.empty())
			return true;

		for (const auto& [key, maskValue] : auxiliaryMask.items()) {
			const auto defaultIt = defaultSettings.find(key);
			if (defaultIt == defaultSettings.end() ||
				defaultIt->is_object() != maskValue.is_object()) {
				return false;
			}

			json completeNode = *defaultIt;
			if (const auto userIt = userSettings.find(key);
				userIt != userSettings.end()) {
				if (userIt->is_object() != maskValue.is_object())
					return false;
				if (userIt->is_object()) {
					MergeJsonByMask(
						completeNode,
						*userIt,
						*userIt);
				} else {
					completeNode = *userIt;
				}
			}
			if (!HasAllJsonValuesForMask(completeNode, maskValue))
				return false;
			materializedSettings[key] = std::move(completeNode);
		}
		return true;
	}

	bool MergeAuxiliarySettingsIntoUserSettings(
		json& userSettings,
		const json& defaultSettings,
		const json& auxiliaryState)
	{
		json materializedSettings;
		if (!MaterializeAuxiliarySettings(
				materializedSettings,
				userSettings,
				defaultSettings,
				auxiliaryState)) {
			return false;
		}

		for (const auto& [key, auxiliaryValue] : auxiliaryState.items()) {
			auto& completeNode = materializedSettings[key];
			MergeJsonByMask(
				completeNode,
				auxiliaryValue,
				auxiliaryValue);
			userSettings[key] = std::move(completeNode);
		}
		return true;
	}

	bool SavePerformanceSettingsToUserDefaults(Feature* feature)
	{
		if (!feature ||
			HasPerformanceDefaultsOverride(feature)) {
			return false;
		}

		json userSettings;
		if (!ReadUserSettingsJson(userSettings))
			return false;
		json defaultSettings;
		if (!ReadSettingsJson(
				Util::PathHelpers::GetSettingsDefaultPath(),
				defaultSettings,
				false,
				"base settings needed for Performance Tuning defaults")) {
			return false;
		}

		try {
			const json currentSettings =
				feature->CapturePerformanceSettingsState();
			const json mask = feature->GetPerformanceTuningUserSettingsMask();
			if (!currentSettings.is_object() ||
				!mask.is_object() ||
				mask.empty() ||
				!HasAllJsonValuesForMask(currentSettings, mask)) {
				throw std::runtime_error(
					"feature performance settings or mask are incomplete");
			}

			const std::string featureName = feature->GetName();
			const auto defaultIt = defaultSettings.find(featureName);
			if (defaultIt == defaultSettings.end() ||
				!defaultIt->is_object()) {
				throw std::runtime_error(
					"base feature settings are missing or malformed");
			}

			json completeSettings = *defaultIt;
			const auto existingIt = userSettings.find(featureName);
			if (existingIt != userSettings.end()) {
				if (!existingIt->is_object()) {
					throw std::runtime_error(
						"existing user feature settings are not an object");
				}
				MergeJsonByMask(
					completeSettings,
					*existingIt,
					*existingIt);
			}
			MergeJsonByMask(
				completeSettings,
				currentSettings,
				mask);
			userSettings[featureName] =
				std::move(completeSettings);

			const json auxiliaryState = feature->CapturePerformanceTuningAuxiliaryState();
			if (!MergeAuxiliarySettingsIntoUserSettings(
					userSettings,
					defaultSettings,
					auxiliaryState)) {
				throw std::runtime_error(
					"could not preserve the complete auxiliary settings container");
			}
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to capture Performance Tuning defaults for {}: {}",
				feature->GetDisplayName(),
				e.what());
			return false;
		} catch (...) {
			logger::warn(
				"Failed to capture Performance Tuning defaults for {}",
				feature->GetDisplayName());
			return false;
		}

		const auto path = Util::PathHelpers::GetSettingsUserPath();
		const auto writeResult = Util::FileHelpers::WriteJsonFileAtomic(path, userSettings, 1);
		if (!writeResult) {
			logger::warn(
				"Failed to atomically update Performance Tuning defaults at {}: {}",
				path.string(),
				writeResult.errorMessage);
			return false;
		}

		logger::info(
			"Saved Performance Tuning user defaults for {}",
			feature->GetDisplayName());
		return true;
	}

	PerformanceUserDefaultsRestoreResult RestorePerformanceSettingsFromUserDefaults(
		Feature* feature)
	{
		if (!feature ||
			HasPerformanceDefaultsOverride(feature)) {
			return PerformanceUserDefaultsRestoreResult::Failed;
		}

		json userSettings;
		if (!ReadUserSettingsJson(userSettings))
			return PerformanceUserDefaultsRestoreResult::Failed;

		json originalSettings;
		json originalAuxiliary;
		json targetSettings;
		json targetAuxiliary;
		json normalizedUserFeatureSettings;
		bool featureSettingsFound = false;
		bool auxiliarySettingsFound = false;
		bool featureSettingsChanged = false;

		try {
			originalSettings =
				feature->CapturePerformanceSettingsState();
			originalAuxiliary = feature->CapturePerformanceTuningAuxiliaryState();
			const json mask = feature->GetPerformanceTuningUserSettingsMask();
			if (!originalSettings.is_object() ||
				!originalAuxiliary.is_object() ||
				!mask.is_object() ||
				mask.empty() ||
				!HasAllJsonValuesForMask(originalSettings, mask)) {
				throw std::runtime_error(
					"feature performance settings or mask are incomplete");
			}
			const auto featureIt = userSettings.find(feature->GetName());
			if (featureIt != userSettings.end() && !featureIt->is_object())
				throw std::runtime_error("saved feature defaults are not an object");
			if (featureIt != userSettings.end()) {
				normalizedUserFeatureSettings = *featureIt;
				if (!feature->NormalizePerformanceTuningUserSettings(
						normalizedUserFeatureSettings)) {
					throw std::runtime_error(
						"saved feature defaults could not be migrated");
				}
			}
			featureSettingsFound =
				featureIt != userSettings.end() &&
				HasJsonValuesForMask(normalizedUserFeatureSettings, mask);
			auxiliarySettingsFound =
				HasJsonValuesForMask(userSettings, originalAuxiliary);

			if (!featureSettingsFound && !auxiliarySettingsFound)
				return PerformanceUserDefaultsRestoreResult::Missing;

			json defaultSettings;
			if (!ReadSettingsJson(
					Util::PathHelpers::GetSettingsDefaultPath(),
					defaultSettings,
					false,
					"base settings needed for Performance Tuning defaults")) {
				throw std::runtime_error("base settings could not be read");
			}

			const auto defaultFeatureIt = defaultSettings.find(feature->GetName());
			if (defaultFeatureIt == defaultSettings.end() ||
				!defaultFeatureIt->is_object()) {
				throw std::runtime_error(
					"base feature settings are missing or malformed");
			}

			json completeFeatureSettings = *defaultFeatureIt;
			if (featureIt != userSettings.end()) {
				MergeJsonByMask(
					completeFeatureSettings,
					normalizedUserFeatureSettings,
					normalizedUserFeatureSettings);
			}
			if (!HasAllJsonValuesForMask(completeFeatureSettings, mask)) {
				throw std::runtime_error(
					"base and user feature defaults are incomplete");
			}

			targetSettings = originalSettings;
			featureSettingsChanged = MergeJsonByMask(
				targetSettings,
				completeFeatureSettings,
				mask);

			json completeAuxiliarySettings;
			if (!MaterializeAuxiliarySettings(
					completeAuxiliarySettings,
					userSettings,
					defaultSettings,
					originalAuxiliary)) {
				throw std::runtime_error(
					"base and user auxiliary defaults are incomplete or malformed");
			}
			targetAuxiliary = originalAuxiliary;
			MergeJsonByMask(
				targetAuxiliary,
				completeAuxiliarySettings,
				originalAuxiliary);
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to prepare Performance Tuning restore for {}: {}",
				feature->GetDisplayName(),
				e.what());
			return PerformanceUserDefaultsRestoreResult::Failed;
		} catch (...) {
			logger::warn(
				"Failed to prepare Performance Tuning restore for {}",
				feature->GetDisplayName());
			return PerformanceUserDefaultsRestoreResult::Failed;
		}

		auto rollback = [&]() {
			bool restored = true;
			try {
				feature->LoadSettings(originalSettings);
				feature->ApplyPerformanceSettings();
			} catch (...) {
				restored = false;
			}
			try {
				if (!feature->RestorePerformanceTuningAuxiliaryState(originalAuxiliary))
					restored = false;
			} catch (...) {
				restored = false;
			}
			try {
				const json verifiedSettings =
					feature->CapturePerformanceSettingsState();
				if (!PerformanceTuning::AreJsonValuesEquivalent(
						verifiedSettings,
						originalSettings) ||
					!PerformanceTuning::AreJsonValuesEquivalent(
						feature->CapturePerformanceTuningAuxiliaryState(),
						originalAuxiliary)) {
					restored = false;
				}
			} catch (...) {
				restored = false;
			}
			if (!restored) {
				logger::error(
					"Performance Tuning could not fully roll back {} after a defaults restore failure",
					feature->GetDisplayName());
			}
		};

		try {
			if (featureSettingsChanged) {
				feature->LoadSettings(targetSettings);
				feature->ApplyPerformanceSettings();
			}
			if (!originalAuxiliary.empty() &&
				!feature->RestorePerformanceTuningAuxiliaryState(targetAuxiliary)) {
				throw std::runtime_error("feature rejected auxiliary user defaults");
			}

			const json restoredSettings =
				feature->CapturePerformanceSettingsState();
			const json restoredAuxiliary =
				feature->CapturePerformanceTuningAuxiliaryState();
			if (!PerformanceTuning::AreJsonValuesEquivalent(
					restoredSettings,
					targetSettings) ||
				!PerformanceTuning::AreJsonValuesEquivalent(
					restoredAuxiliary,
					targetAuxiliary)) {
				throw std::runtime_error(
					"feature did not apply the requested user defaults exactly");
			}
			const bool changed =
				!PerformanceTuning::AreJsonValuesEquivalent(
					restoredSettings,
					originalSettings) ||
				!PerformanceTuning::AreJsonValuesEquivalent(
					restoredAuxiliary,
					originalAuxiliary);
			if (!changed)
				return PerformanceUserDefaultsRestoreResult::Unchanged;
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to restore Performance Tuning defaults for {}: {}",
				feature->GetDisplayName(),
				e.what());
			rollback();
			return PerformanceUserDefaultsRestoreResult::Failed;
		} catch (...) {
			logger::warn(
				"Failed to restore Performance Tuning defaults for {}",
				feature->GetDisplayName());
			rollback();
			return PerformanceUserDefaultsRestoreResult::Failed;
		}

		logger::info(
			"Restored Performance Tuning user defaults for {}",
			feature->GetDisplayName());
		return PerformanceUserDefaultsRestoreResult::Restored;
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
		const bool overrideManaged =
			HasPerformanceDefaultsOverride(feature);
		if (overrideManaged) {
			message = T(
				TKEY("defaults.override_managed"),
				"Defaults are managed by an installed settings override.");
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(disabled || overrideManaged);
		if (RenderUserDefaultsIconButton(
				applyId.c_str(),
				T(TKEY("defaults.save"), "Apply settings to user defaults"),
				icons.saveSettings.texture,
				imageSize)) {
			message = SavePerformanceSettingsToUserDefaults(feature) ?
			              T(TKEY("defaults.saved"), "Performance user defaults updated.") :
			              T(TKEY("defaults.save_failed"), "Failed to update performance user defaults.");
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				overrideManaged ?
					T(
						TKEY("defaults.override_tooltip"),
						"User-default actions are unavailable because this feature uses the higher-priority settings override system.") :
					T(
						TKEY("defaults.save_tooltip"),
						"Apply the current Performance Tuning controls for this feature to user defaults."));
		}

		ImGui::SameLine();
		if (RenderUserDefaultsIconButton(
				restoreId.c_str(),
				T(TKEY("defaults.restore"), "Reset to user defaults"),
				icons.loadSettings.texture,
				imageSize)) {
			switch (RestorePerformanceSettingsFromUserDefaults(feature)) {
			case PerformanceUserDefaultsRestoreResult::Restored:
				settingsRestored = true;
				message = T(TKEY("defaults.restored"), "Performance user defaults restored.");
				break;
			case PerformanceUserDefaultsRestoreResult::Unchanged:
				message = T(TKEY("defaults.unchanged"), "Already using performance user defaults.");
				break;
			case PerformanceUserDefaultsRestoreResult::Missing:
				message = T(TKEY("defaults.missing"), "No saved performance user defaults found.");
				break;
			case PerformanceUserDefaultsRestoreResult::Failed:
			default:
				message = T(TKEY("defaults.restore_failed"), "Failed to restore performance user defaults.");
				break;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				overrideManaged ?
					T(
						TKEY("defaults.override_tooltip"),
						"User-default actions are unavailable because this feature uses the higher-priority settings override system.") :
					T(
						TKEY("defaults.restore_tooltip"),
						"Reset the current Performance Tuning controls for this feature to saved user defaults."));
		}
		ImGui::EndDisabled();

		if (!message.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", message.c_str());
		}
		return settingsRestored;
	}

	bool TryRestoreFeatureCostOriginalState(
		Feature* feature,
		FeatureCostMeasurementState& state)
	{
		if (!state.restorePending && !state.featureStateRestorePending &&
			!state.autoVanityCameraSuppressionAcquired)
			return true;

		bool featureRestored = !state.featureStateRestorePending;
		if (state.featureStateRestorePending && feature) {
			try {
				feature->RestorePerformanceCostMeasurementState(state.originalState);
				featureRestored = true;
				state.featureStateRestorePending = false;
			} catch (const std::exception& e) {
				logger::error(
					"Failed to restore {} after Performance Tuning: {}",
					feature->GetDisplayName(),
					e.what());
			} catch (...) {
				logger::error(
					"Failed to restore {} after Performance Tuning",
					feature->GetDisplayName());
			}
		} else if (state.featureStateRestorePending) {
			// The feature object no longer exists, so there is no live target that
			// can be restored or retried.
			logger::error(
				"Could not restore a removed feature after Performance Tuning");
			state.featureStateRestorePending = false;
			featureRestored = true;
		}

		bool vanityCameraRestored =
			!state.autoVanityCameraSuppressionAcquired;
		if (state.autoVanityCameraSuppressionAcquired) {
			if (Util::ReleaseAutoVanityCameraSuppression()) {
				state.autoVanityCameraSuppressionAcquired = false;
				vanityCameraRestored = true;
			} else {
				logger::error(
					"Failed to restore automatic vanity camera after Performance Tuning");
			}
		}

		state.restorePending =
			state.featureStateRestorePending ||
			state.autoVanityCameraSuppressionAcquired;
		return featureRestored && vanityCameraRestored &&
		       !state.restorePending;
	}

	void ReopenMenuAfterFeatureCostMeasurement(
		FeatureCostMeasurementState& state)
	{
		if (!state.menuClosedForMeasurement)
			return;

		if (auto* menu = Menu::GetSingleton())
			menu->IsEnabled = true;
		state.menuClosedForMeasurement = false;
	}

	void FailFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		std::string reason)
	{
		const bool restored = TryRestoreFeatureCostOriginalState(feature, state);
		state.stage = FeatureCostMeasurementStage::Failed;
		state.failureReason = std::move(reason);
		if (!restored) {
			if (!state.failureReason.empty())
				state.failureReason += " ";
			state.failureReason += T(
				TKEY("error.restore_pending"),
				"Original settings could not be restored; retry restoration.");
		}
		ReopenMenuAfterFeatureCostMeasurement(state);
	}

	bool PrepareFeatureCostMeasurementsForSceneChange(
		bool cancelAllRunning,
		const char* failureReason)
	{
		bool allRestored = true;
		for (auto& [shortName, state] : g_costMeasurementStates) {
			auto* feature = FindFeatureByShortName(shortName);
			if (IsFeatureCostMeasurementRunning(state)) {
				if (cancelAllRunning) {
					FailFeatureCostMeasurement(
						feature,
						state,
						failureReason ? failureReason :
										T(TKEY("error.scene_changed"), "Stopped because the scene changed."));
				}
			} else if (state.restorePending) {
				TryRestoreFeatureCostOriginalState(feature, state);
			}
			allRestored &= !state.restorePending;
		}

		SyncProfilerCaptureModeLimit();
		return allRestored;
	}

	void BeginFeatureCostCapture(
		FeatureCostMeasurementState& state,
		FeatureCostMeasurementLeg leg,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		state.leg = leg;
		auto& window = GetFeatureCostSampleWindow(state);
		PerformanceTuning::BeginSampleWindow(
			window,
			summary.presentIntervalSampleId,
			summary.wholeFrameSampleId,
			true,
			summary.outputPresentSampleId,
			summary.outputPresentDiscontinuityEpoch);
		state.profilerSkippedCaptureStart =
			summary.skippedWholeFrameCaptureCount;
		state.stage = FeatureCostMeasurementStage::Measuring;
		state.stageStartTime = currentTime;
		state.lastProgressTime = currentTime;
		state.lastObservedPresentSampleId = summary.presentIntervalSampleId;
	}

	bool ApplyFeatureCostComparisonState(
		Feature* feature,
		FeatureCostMeasurementState& state)
	{
		if (!feature)
			return false;

		state.featureStateRestorePending = true;
		state.restorePending = true;
		try {
			feature->SetPerformanceCostMeasurementEnabled(false);
			return true;
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to apply Performance Tuning comparison state for {}: {}",
				feature->GetDisplayName(),
				e.what());
		} catch (...) {
			logger::warn(
				"Failed to apply Performance Tuning comparison state for {}",
				feature->GetDisplayName());
		}
		return false;
	}

	void StartFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		if (!feature ||
			!feature->SupportsPerformanceCostMeasurement() ||
			!feature->IsPerformanceCostMeasurementEnabled() ||
			!IsFeatureCostEnvironmentEligible() ||
			GetFeatureCostCooldownRemaining(currentTime) > 0.0) {
			return;
		}

		state = {};
		try {
			state.originalState =
				feature->CapturePerformanceCostMeasurementState();
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to capture the original Performance Tuning state for {}: {}",
				feature->GetDisplayName(),
				e.what());
			state.stage = FeatureCostMeasurementStage::Failed;
			state.failureReason = T(
				TKEY("error.capture_failed"),
				"Could not capture the original feature state.");
			return;
		} catch (...) {
			state.stage = FeatureCostMeasurementStage::Failed;
			state.failureReason = T(
				TKEY("error.capture_failed"),
				"Could not capture the original feature state.");
			return;
		}

		if (Util::IsAutoVanityCameraActive()) {
			state.stage = FeatureCostMeasurementStage::Failed;
			state.failureReason = T(
				TKEY("error.vanity_camera_active"),
				"Exit the automatic camera view before measuring.");
			return;
		}
		if (!Util::AcquireAutoVanityCameraSuppression()) {
			state.stage = FeatureCostMeasurementStage::Failed;
			state.failureReason = T(
				TKEY("error.vanity_camera_prepare"),
				"Could not prevent automatic camera movement during measurement.");
			return;
		}
		state.autoVanityCameraSuppressionAcquired = true;
		state.restorePending = true;

		const auto& upscaling = globals::features::upscaling;
		state.frameGenerationConfigured =
			upscaling.IsFrameGenerationConfigured();
		state.fpsLimitConfigured =
			upscaling.IsFrameRateLimitConfigured();

		state.overallStartTime = currentTime;
		state.timingDiscontinuityEpoch =
			summary.presentDiscontinuityEpoch;
		state.leg = FeatureCostMeasurementLeg::Current;
		state.stage = FeatureCostMeasurementStage::InitialDelay;
		state.stageStartTime = currentTime;
		state.lastProgressTime = currentTime;
		state.lastObservedPresentSampleId =
			summary.presentIntervalSampleId;

		if (auto* menu = Menu::GetSingleton()) {
			state.menuClosedForMeasurement = menu->IsEnabled;
			menu->IsEnabled = false;
		}
	}

	PerformanceTuning::AddSampleResult AddWholeFrameTimingSample(
		PerformanceTuning::SampleWindow& window,
		const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		std::optional<double> gpuMs;
		std::optional<double> cpuMs;
		if (summary.hasWholeFrameGpuSample &&
			summary.wholeFrameGpuSampleId == summary.wholeFrameSampleId)
			gpuMs = summary.wholeFrameGpuSampleMs;
		if (summary.hasWholeFrameCpuSample &&
			summary.wholeFrameCpuSampleId == summary.wholeFrameSampleId)
			cpuMs = summary.wholeFrameCpuSampleMs;

		return PerformanceTuning::AddWholeFrameSample(
			window,
			summary.wholeFrameSampleId,
			summary.wholeFramePresentIntervalSampleId,
			gpuMs,
			cpuMs);
	}

	bool HasRequiredWholeFrameCoverage(
		const PerformanceTuning::SampleWindow& window)
	{
		const auto diagnostics =
			PerformanceTuning::BuildWindowDiagnostics(window);
		if (!diagnostics.wholeFrameGpu.Meets(
				PerformanceTuning::kDefaultMinimumMetricCoverage) ||
			!diagnostics.wholeFrameCpu.Meets(
				PerformanceTuning::kDefaultMinimumMetricCoverage)) {
			return false;
		}
		for (const auto& block : diagnostics.blocks) {
			if (!block.wholeFrameGpu.Meets(
					PerformanceTuning::kDefaultMinimumMetricCoverage) ||
				!block.wholeFrameCpu.Meets(
					PerformanceTuning::kDefaultMinimumMetricCoverage)) {
				return false;
			}
		}
		return true;
	}

	bool CompleteFeatureCostWindow(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		if (state.leg == FeatureCostMeasurementLeg::Current) {
			if (!ApplyFeatureCostComparisonState(feature, state)) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.comparison_apply"),
						"Stopped because the comparison state could not be applied."));
				return false;
			}
			state.leg = FeatureCostMeasurementLeg::Comparison;
			state.stage = FeatureCostMeasurementStage::ComparisonDelay;
			state.stageStartTime = currentTime;
			state.lastProgressTime = currentTime;
			state.lastObservedPresentSampleId =
				summary.presentIntervalSampleId;
			return true;
		}

		state.result = PerformanceTuning::CalculateCostResult(
			state.current,
			state.comparison);
		if (!TryRestoreFeatureCostOriginalState(feature, state)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.restore_failed"),
					"Stopped because the original feature state could not be restored."));
			return false;
		}
		try {
			state.resultSettingsState = CapturePerformanceUiState(feature);
		} catch (...) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.result_snapshot"),
					"Stopped because the result settings snapshot could not be captured."));
			return false;
		}
		state.stage = FeatureCostMeasurementStage::Complete;
		g_featureCostCooldownUntilTime =
			currentTime + kFeatureCostCooldownSeconds;
		ReopenMenuAfterFeatureCostMeasurement(state);
		return true;
	}

	void UpdateFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		if (!feature || !IsFeatureCostMeasurementRunning(state))
			return;
		Util::MaintainAutoVanityCameraSuppression();

		if (currentTime - state.overallStartTime >=
			kFeatureCostOverallTimeoutSeconds) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(TKEY("error.overall_timeout"), "Stopped because the measurement timed out."));
			return;
		}
		if (!IsFeatureCostEnvironmentEligible()) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.environment_changed"),
					"Stopped because the game was paused, loading, or lost focus."));
			return;
		}
		if (summary.presentDiscontinuityEpoch !=
			state.timingDiscontinuityEpoch) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.timing_discontinuity"),
					"Stopped because frame timing was interrupted."));
			return;
		}
		if (summary.presentIntervalSampleId != 0) {
			if (state.lastObservedPresentSampleId != 0 &&
				summary.presentIntervalSampleId < state.lastObservedPresentSampleId) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.timing_reset"),
						"Stopped because the frame timing source was reset."));
				return;
			}
			if (state.stage != FeatureCostMeasurementStage::Draining &&
				state.lastObservedPresentSampleId != 0 &&
				summary.presentIntervalSampleId >
					state.lastObservedPresentSampleId &&
				summary.presentIntervalSampleId -
						state.lastObservedPresentSampleId >
					1) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.timing_gap"),
						"Stopped because one or more direct frame timing samples were missed."));
				return;
			}
			if (summary.presentIntervalSampleId >
				state.lastObservedPresentSampleId) {
				state.lastObservedPresentSampleId =
					summary.presentIntervalSampleId;
				state.lastProgressTime = currentTime;
			}
		}
		if (currentTime - state.lastProgressTime >
			kFeatureCostSampleProgressTimeoutSeconds) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.no_progress"),
					"Stopped because no new presented frames were measured."));
			return;
		}

		if (state.stage == FeatureCostMeasurementStage::InitialDelay) {
			if (currentTime - state.stageStartTime <
				kFeatureCostInitialWaitSeconds) {
				return;
			}
			BeginFeatureCostCapture(
				state,
				FeatureCostMeasurementLeg::Current,
				summary,
				currentTime);
			return;
		}

		if (state.stage == FeatureCostMeasurementStage::ComparisonDelay) {
			if (currentTime - state.stageStartTime <
				kFeatureCostComparisonWaitSeconds) {
				return;
			}
			BeginFeatureCostCapture(
				state,
				FeatureCostMeasurementLeg::Comparison,
				summary,
				currentTime);
			return;
		}

		auto& window = GetFeatureCostSampleWindow(state);
		if (summary.hasOutputPresentSample) {
			PerformanceTuning::AddOutputPresentSample(
				window,
				summary.outputPresentSampleId,
				summary.outputPresentDiscontinuityEpoch,
				summary.outputPresentSampleDurationMs,
				summary.outputPresentedFrameCount);
		}
		const auto wholeFrameResult =
			AddWholeFrameTimingSample(window, summary);
		if (wholeFrameResult ==
			PerformanceTuning::AddSampleResult::SourceReset) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.timing_reset"),
					"Stopped because the frame timing source was reset."));
			return;
		}

		if (state.stage == FeatureCostMeasurementStage::Measuring) {
			if (currentTime - state.stageStartTime >
				kFeatureCostMeasurementMaximumSeconds) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.insufficient_frames"),
						"Stopped because too few valid game frames were produced before the capture deadline."));
				return;
			}

			if (!summary.hasPresentIntervalSample) {
				if (currentTime - state.stageStartTime >=
					kFeatureCostMeasurementMaximumSeconds) {
					FailFeatureCostMeasurement(
						feature,
						state,
						T(
							TKEY("error.insufficient_frames"),
							"Stopped because too few valid game frames were produced before the capture deadline."));
				}
				return;
			}
			const auto presentResult = PerformanceTuning::AddPresentSample(
				window,
				summary.presentIntervalSampleId,
				summary.presentIntervalSampleMs,
				summary.presentIntervalSynced);
			if (presentResult ==
				PerformanceTuning::AddSampleResult::SourceReset) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.timing_reset"),
						"Stopped because the frame timing source was reset."));
				return;
			}
			if (presentResult ==
				PerformanceTuning::AddSampleResult::SourceGap) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.timing_gap"),
						"Stopped because one or more direct frame timing samples were missed."));
				return;
			}
			if (presentResult ==
					PerformanceTuning::AddSampleResult::IntervalTooLarge ||
				presentResult ==
					PerformanceTuning::AddSampleResult::InvalidValue) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.invalid_present"),
						"Stopped because a presented-frame interval was invalid or indicated an interruption."));
				return;
			}
			if (presentResult ==
				PerformanceTuning::AddSampleResult::Complete) {
				state.stage = FeatureCostMeasurementStage::Draining;
				state.stageStartTime = currentTime;
			} else if (currentTime - state.stageStartTime >=
					   kFeatureCostMeasurementMaximumSeconds) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.insufficient_frames"),
						"Stopped because too few valid game frames were produced before the capture deadline."));
			}
			return;
		}

		if (state.stage != FeatureCostMeasurementStage::Draining)
			return;

		const bool profilerDrained =
			window.endPresentSampleId != 0 &&
			window.latestWholeFramePresentSampleId >=
				window.endPresentSampleId;
		const bool drainTimedOut =
			currentTime - state.stageStartTime >=
			kFeatureCostProfilerDrainTimeoutSeconds;
		if (!profilerDrained && !drainTimedOut)
			return;
		const auto counterDelta = [](uint64_t current, uint64_t start) {
			return current >= start ? current - start : 0;
		};
		const uint64_t skippedCaptureCount = counterDelta(
			summary.skippedWholeFrameCaptureCount,
			state.profilerSkippedCaptureStart);

		const bool instrumentationComplete =
			profilerDrained &&
			skippedCaptureCount == 0 &&
			HasRequiredWholeFrameCoverage(window);
		if (!instrumentationComplete) {
			if (!profilerDrained || skippedCaptureCount > 0) {
				// Never accept an apparently high percentage when the missing data
				// are the still-pending tail of the window. Tail-correlated query
				// loss can bias a mean even when fewer than 10% are outstanding.
				window.wholeFrameGpuCoverageDiscontinuous = true;
				window.wholeFrameCpuCoverageDiscontinuous = true;
			}
		}

		CompleteFeatureCostWindow(
			feature,
			state,
			summary,
			currentTime);
	}

	int GetDirectionFromFrameTimeDelta(
		float deltaMs,
		float baselineMs = 0.0f)
	{
		const float practicalFloor = std::max(
			kTuningDeltaThresholdMs,
			std::abs(baselineMs) *
				static_cast<float>(PerformanceTuning::kPracticalFloorRelative));
		if (std::abs(deltaMs) <= practicalFloor)
			return 0;
		return deltaMs > 0.0f ? 1 : -1;
	}

	int GetDirectionFromFeatureCostMetric(
		const PerformanceTuning::MetricDelta& metric)
	{
		if (!metric.IsReliable())
			return 0;
		return metric.direction;
	}

	int GetDirectionFromFeatureCostFps(
		const PerformanceTuning::FpsDelta& fps)
	{
		if (!fps.IsAvailable() || !fps.IsReliable())
			return 0;
		return *fps.value < 0.0 ? 1 : -1;
	}

	bool TryGetDisplayTimingMs(bool hasTiming, float timingMs, float& value)
	{
		if (hasTiming && std::isfinite(timingMs) && timingMs > 0.0f) {
			value = timingMs;
			return true;
		}
		return false;
	}

	bool TryGetDisplayGpuMs(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		float& value)
	{
		return TryGetDisplayTimingMs(
			summary.hasWholeFrameGpu,
			summary.wholeFrameGpuMs,
			value);
	}

	bool TryGetDisplayCpuMs(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		float& value)
	{
		return TryGetDisplayTimingMs(
			summary.hasWholeFrameCpu,
			summary.wholeFrameCpuMs,
			value);
	}

	void RenderMetricCounter(
		const char* id,
		const char* label,
		float value,
		const char* format,
		int direction,
		bool valid,
		const char* tooltip)
	{
		ImGui::PushID(id);
		if (direction != 0)
			ImGui::PushStyleColor(
				ImGuiCol_Border,
				Util::Color::PerformanceDelta(direction));

		const float height = 58.0f * Util::GetUIScale();
		if (ImGui::BeginChild(
				"##Counter",
				ImVec2(0.0f, height),
				true,
				ImGuiWindowFlags_NoScrollbar |
					ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::TextDisabled("%s", label);
			if (!valid) {
				ImGui::TextDisabled("--");
			} else if (direction != 0) {
				ImGui::TextColored(
					Util::Color::PerformanceDelta(direction),
					format,
					value);
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
		const bool hasDisplayGpu =
			TryGetDisplayGpuMs(summary, displayGpuMs);
		float displayCpuMs = 0.0f;
		const bool hasDisplayCpu =
			TryGetDisplayCpuMs(summary, displayCpuMs);

		if (ImGui::BeginTable(
				"##PerformanceTuningTopCounters",
				4,
				ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_PadOuterX)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			RenderMetricCounter(
				"Total",
				T(TKEY("counter.game_frame"), "Game frame:"),
				summary.presentIntervalMs,
				"%.2f ms",
				highlightState.frameDirection,
				summary.hasPresentInterval,
				T(
					TKEY("counter.game_frame_tooltip"),
					"Direct Present-to-Present wall-clock time for each game-produced frame. It includes VSync, FPS limiting, and Present wait, and is the pre-frame-generation cadence."));
			ImGui::TableNextColumn();
			RenderMetricCounter(
				"GPU",
				T(TKEY("counter.gpu"), "GPU:"),
				displayGpuMs,
				"%.2f ms",
				highlightState.gpuTotalDirection,
				hasDisplayGpu,
				T(
					TKEY("counter.gpu_tooltip"),
					"D3D11 timestamp duration across the whole rendered game frame. It excludes Present wait."));
			ImGui::TableNextColumn();
			RenderMetricCounter(
				"CPU",
				T(TKEY("counter.cpu"), "CPU:"),
				displayCpuMs,
				"%.2f ms",
				highlightState.cpuTotalDirection,
				hasDisplayCpu,
				T(
					TKEY("counter.cpu_tooltip"),
					"CPU wall time across the whole rendered game frame up to Present. It excludes Present wait."));
			ImGui::TableNextColumn();
			RenderMetricCounter(
				"FPS",
				T(TKEY("counter.game_fps"), "Game FPS:"),
				summary.fps,
				"%.0f",
				highlightState.fpsDirection,
				summary.fps > 0.0f,
				T(
					TKEY("counter.game_fps_tooltip"),
					"1000 divided by direct Present-to-Present time. With frame generation, displayed output FPS can be higher."));
			ImGui::EndTable();
		}
	}

	void RenderCostSignificance(const std::optional<double>& pValue)
	{
		if (!pValue) {
			ImGui::TextDisabled("--");
		} else if (*pValue <=
				   PerformanceTuning::kStatisticalSignificanceLevel) {
			ImGui::Text(
				T(TKEY("result.significant_yes"), "Yes (p=%.3f)"),
				*pValue);
		} else {
			ImGui::TextDisabled(
				T(TKEY("result.significant_no"), "No (p=%.3f)"),
				*pValue);
		}
	}

	std::string BuildCadenceContextSummary(
		const FeatureCostMeasurementState& state)
	{
		std::string summary;
		auto append = [&](const char* sentence) {
			if (!summary.empty())
				summary += ' ';
			summary += sentence;
		};

		if (state.frameGenerationConfigured) {
			append(T(
				TKEY("result.context_frame_generation"),
				"Frame Generation was enabled in settings."));
		}
		if (state.fpsLimitConfigured) {
			append(T(
				TKEY("result.context_fps_limit"),
				"An FPS limiter was enabled."));
		} else if (state.result.presentSynced) {
			append(T(
				TKEY("result.context_vsync"),
				"VSync pacing was detected."));
		} else if (state.result.framePaced) {
			append(T(
				TKEY("result.context_frame_pacing"),
				"External frame pacing was detected."));
		}
		return summary;
	}

	void RenderCadenceMarkerTooltip(
		const FeatureCostMeasurementState& state);

	void RenderCostResultRow(
		const char* label,
		const std::optional<double>& value,
		const std::optional<double>& standardError,
		const std::optional<double>& pValue,
		int direction,
		const char* valueFormat,
		const char* errorFormat,
		const FeatureCostMeasurementState* cadenceContext)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("%s%s", label, cadenceContext ? "*" : "");
		if (cadenceContext)
			RenderCadenceMarkerTooltip(*cadenceContext);

		ImGui::TableNextColumn();
		if (!value) {
			ImGui::TextDisabled("--");
		} else {
			if (direction != 0)
				ImGui::PushStyleColor(
					ImGuiCol_Text,
					Util::Color::PerformanceDelta(direction));
			ImGui::Text(valueFormat, *value);
			if (direction != 0)
				ImGui::PopStyleColor();
		}

		ImGui::TableNextColumn();
		if (standardError)
			ImGui::Text(errorFormat, *standardError);
		else
			ImGui::TextDisabled("--");

		ImGui::TableNextColumn();
		RenderCostSignificance(pValue);
	}

	void RenderCadenceMarkerTooltip(
		const FeatureCostMeasurementState& state)
	{
		if (auto _tt = Util::HoverTooltipWrapper()) {
			const auto summary = BuildCadenceContextSummary(state);
			ImGui::TextWrapped("%s", summary.c_str());
		}
	}

	void RenderFeatureCostResults(
		const FeatureCostMeasurementState& state,
		std::string_view comparisonLabel)
	{
		if (!ImGui::BeginTable(
				"##FeatureCostResults",
				4,
				ImGuiTableFlags_Borders |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_SizingStretchProp)) {
			return;
		}

		ImGui::TableSetupColumn(
			T(TKEY("result.column_metric"), "Metric"),
			ImGuiTableColumnFlags_WidthStretch,
			1.4f);
		std::string deltaColumnLabel =
			T(TKEY("result.column_delta"), "Delta vs");
		deltaColumnLabel += ' ';
		deltaColumnLabel += comparisonLabel;
		ImGui::TableSetupColumn(
			deltaColumnLabel.c_str(),
			ImGuiTableColumnFlags_WidthStretch,
			1.0f);
		ImGui::TableSetupColumn(
			T(TKEY("result.column_error"), "Error (SE)"),
			ImGuiTableColumnFlags_WidthStretch,
			1.0f);
		ImGui::TableSetupColumn(
			T(TKEY("result.column_significance"), "Significant"),
			ImGuiTableColumnFlags_WidthStretch,
			1.2f);
		ImGui::TableHeadersRow();

		const bool gameCadenceMarked =
			state.frameGenerationConfigured ||
			state.fpsLimitConfigured ||
			state.result.presentSynced || state.result.framePaced;
		const bool outputCadenceMarked =
			state.fpsLimitConfigured ||
			state.result.presentSynced || state.result.framePaced;
		RenderCostResultRow(
			T(TKEY("result.total"), "Game frame"),
			state.result.present.valueMs,
			state.result.present.standardErrorMs,
			state.result.present.pValue,
			GetDirectionFromFeatureCostMetric(state.result.present),
			"%+.3f ms",
			"%.3f ms",
			gameCadenceMarked ? &state : nullptr);

		RenderCostResultRow(
			T(TKEY("result.gpu"), "Whole-frame GPU"),
			state.result.wholeFrameGpu.valueMs,
			state.result.wholeFrameGpu.standardErrorMs,
			state.result.wholeFrameGpu.pValue,
			GetDirectionFromFeatureCostMetric(state.result.wholeFrameGpu),
			"%+.3f ms",
			"%.3f ms",
			nullptr);
		RenderCostResultRow(
			T(TKEY("result.cpu"), "Whole-frame CPU"),
			state.result.wholeFrameCpu.valueMs,
			state.result.wholeFrameCpu.standardErrorMs,
			state.result.wholeFrameCpu.pValue,
			GetDirectionFromFeatureCostMetric(state.result.wholeFrameCpu),
			"%+.3f ms",
			"%.3f ms",
			nullptr);
		RenderCostResultRow(
			T(TKEY("result.fps"), "Game FPS"),
			state.result.fps.value,
			state.result.fps.standardError,
			state.result.fps.pValue,
			GetDirectionFromFeatureCostFps(state.result.fps),
			"%+.1f FPS",
			"%.1f FPS",
			gameCadenceMarked ? &state : nullptr);
		if (state.frameGenerationConfigured) {
			RenderCostResultRow(
				T(TKEY("result.output_fps"), "Output FPS"),
				state.result.outputFps.value,
				state.result.outputFps.standardError,
				state.result.outputFps.pValue,
				GetDirectionFromFeatureCostFps(
					state.result.outputFps),
				"%+.1f FPS",
				"%.1f FPS",
				outputCadenceMarked ? &state : nullptr);
		}

		ImGui::EndTable();
		if (gameCadenceMarked) {
			const auto cadenceSummary = BuildCadenceContextSummary(state);
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped(
				T(
					TKEY("result.context_footnote"),
					"* Game frame and Game FPS use game-produced cadence, not generated output FPS. Starred metrics were measured under: %s"),
				cadenceSummary.c_str());
			ImGui::PopStyleColor();
		}
	}

	void RenderFeatureCostRunningStatus(
		Feature* feature,
		FeatureCostMeasurementState& state)
	{
		const auto progress =
			BuildFeatureCostProgressText(state, ImGui::GetTime());
		if (!progress.empty())
			ImGui::TextDisabled("%s", progress.c_str());

		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("action.cancel"), "Cancel"))) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(TKEY("error.cancelled"), "Measurement cancelled."));
		}
	}

	bool RenderFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (!feature)
			return false;

		const bool hasState =
			state.stage != FeatureCostMeasurementStage::Idle;
		if (!feature->SupportsPerformanceCostMeasurement() && !hasState) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::TextDisabled(
				"%s",
				T(
					TKEY("cost.unavailable"),
					"Actual feature cost is unavailable."));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped(
					"%s",
					T(
						TKEY("cost.unavailable_reason"),
						"This feature has no runtime inactive state to compare."));
			}
			return false;
		}
		if (!feature->IsPerformanceCostMeasurementEnabled() && !hasState)
			return false;

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (IsFeatureCostMeasurementRunning(state)) {
			RenderFeatureCostRunningStatus(feature, state);
			return false;
		}

		const bool anyMeasurementLocked =
			IsAnyFeatureCostMeasurementLocked();
		const bool environmentEligible =
			IsFeatureCostEnvironmentEligible();
		const double cooldownRemaining =
			GetFeatureCostCooldownRemaining(currentTime);
		const bool canStart =
			feature->SupportsPerformanceCostMeasurement() &&
			feature->IsPerformanceCostMeasurementEnabled() &&
			environmentEligible &&
			!anyMeasurementLocked && cooldownRemaining <= 0.0;

		ImGui::BeginDisabled(!canStart);
		const bool startClicked = ImGui::Button(
			T(TKEY("cost.start"), "Measure actual feature cost"));
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			const auto config = feature->GetPerformanceTuningConfig();
			ImGui::TextWrapped(
				T(
					TKEY("cost.tooltip"),
					"Measures the current setting in five one-second samples, switches to None or Off, waits five seconds, then measures five one-second comparison samples. Delayed GPU and CPU samples are matched to the same produced frames. The complete run has a 30-second hard deadline."));
			ImGui::TextWrapped(
				T(TKEY("cost.comparison"), "Comparison: %s - %s"),
				config.comparisonLabel.data(),
				config.comparisonDetails.data());
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("cost.tooltip_scene"),
					"Automatic vanity rotation is suspended during the run. Keep the camera and scene reasonably still. Pause, loading, focus loss, or a timing interruption stops the run; ordinary animation remains part of the average."));
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("cost.tooltip_cadence"),
					"Frame Generation and FPS limits are not changed independently of the comparison shown above. Completed results mark game-produced cadence with * when Frame Generation or frame limiting/pacing affects the measurement context."));
		}
		if (cooldownRemaining > 0.0) {
			ImGui::SameLine();
			ImGui::TextDisabled(
				T(TKEY("cost.cooldown"), "Ready in %.1f s"),
				cooldownRemaining);
		}

		if (state.stage == FeatureCostMeasurementStage::Failed) {
			ImGui::SameLine();
			ImGui::TextColored(
				Util::Colors::GetWarning(),
				"%s",
				state.failureReason.empty() ?
					T(TKEY("error.generic"), "Measurement stopped.") :
					state.failureReason.c_str());
			if (state.restorePending) {
				if (ImGui::Button(
						T(TKEY("action.retry_restore"), "Retry restoration"))) {
					if (TryRestoreFeatureCostOriginalState(feature, state)) {
						state.failureReason = T(
							TKEY("status.restore_succeeded"),
							"Original feature settings restored.");
					}
				}
			}
			return startClicked;
		}

		if (state.stage != FeatureCostMeasurementStage::Complete)
			return startClicked;

		ImGui::Spacing();
		RenderFeatureCostResults(
			state,
			feature->GetPerformanceTuningConfig().comparisonLabel);
		return startClicked;
	}

	std::vector<Feature*> BuildPerformanceFeatureList()
	{
		std::vector<Feature*> features;
		const bool essentialsMode =
			globals::menu && globals::menu->IsPerformanceUiMode();
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature ||
				!feature->loaded ||
				feature->IsHiddenFromUserView() ||
				(essentialsMode && feature->IsHiddenInEssentialsMode()) ||
				!feature->IsInMenu() ||
				!feature->HasPerformanceSettings() ||
				feature->GetPerformanceTuningConfig().order < 0) {
				continue;
			}
			features.push_back(feature);
		}

		std::ranges::sort(features, [](Feature* lhs, Feature* rhs) {
			const int lhsOrder =
				lhs->GetPerformanceTuningConfig().order;
			const int rhsOrder =
				rhs->GetPerformanceTuningConfig().order;
			if (lhsOrder != rhsOrder)
				return lhsOrder < rhsOrder;
			return lhs->GetDisplayName() < rhs->GetDisplayName();
		});
		return features;
	}

	std::vector<std::string> BuildPerformanceFeaturePrefixes(
		const std::vector<Feature*>& features)
	{
		std::vector<std::string> prefixes;
		prefixes.reserve(features.size());
		for (auto* feature : features) {
			if (feature)
				prefixes.push_back(feature->GetShortName());
		}
		return prefixes;
	}

	std::string FindLockedFeatureShortName()
	{
		for (const auto& [shortName, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementRunning(state))
				return shortName;
		}
		for (const auto& [shortName, state] : g_costMeasurementStates) {
			if (state.restorePending)
				return shortName;
		}
		return {};
	}

	Feature* FindSelectedFeature(const std::vector<Feature*>& features)
	{
		if (features.empty())
			return nullptr;

		const auto it = std::ranges::find_if(
			features,
			[](Feature* feature) {
				return feature &&
			           feature->GetShortName() == g_selectedShortName;
			});
		if (it != features.end())
			return *it;

		g_selectedShortName = features.front()->GetShortName();
		return features.front();
	}

	Feature* FindRunningFeature()
	{
		for (auto& [shortName, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementRunning(state))
				return FindFeatureByShortName(shortName);
		}
		return nullptr;
	}

	void UpdateRunningFeatureCostMeasurements(
		const ProfilingRenderer::PerformanceTimingSummary& timing,
		double currentTime)
	{
		for (auto& [shortName, state] : g_costMeasurementStates) {
			if (!IsFeatureCostMeasurementRunning(state))
				continue;

			auto* feature = FindFeatureByShortName(shortName);
			if (!feature || !feature->loaded) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.feature_unavailable"),
						"Stopped because the measured feature became unavailable."));
				continue;
			}
			UpdateFeatureCostMeasurement(
				feature,
				state,
				timing,
				currentTime);
		}
	}

	ProfilingRenderer::PerformanceTimingTotals GetTimingTotalsForFeature(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const std::string& shortName)
	{
		ProfilingRenderer::PerformanceTimingTotals totals;
		const auto it = summary.features.find(shortName);
		if (it == summary.features.end())
			return totals;
		return it->second;
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
		if (TryGetDisplayGpuMs(state.baseline, baselineGpuMs) &&
			TryGetDisplayGpuMs(current, currentGpuMs)) {
			state.gpuTotalDirection =
				GetDirectionFromFrameTimeDelta(
					currentGpuMs - baselineGpuMs,
					baselineGpuMs);
		}
		float baselineCpuMs = 0.0f;
		float currentCpuMs = 0.0f;
		if (TryGetDisplayCpuMs(state.baseline, baselineCpuMs) &&
			TryGetDisplayCpuMs(current, currentCpuMs)) {
			state.cpuTotalDirection =
				GetDirectionFromFrameTimeDelta(
					currentCpuMs - baselineCpuMs,
					baselineCpuMs);
		}
		if (state.baseline.hasPresentInterval &&
			current.hasPresentInterval) {
			state.frameDirection = GetDirectionFromFrameTimeDelta(
				current.presentIntervalMs -
					state.baseline.presentIntervalMs,
				state.baseline.presentIntervalMs);
			state.fpsDirection = state.frameDirection;
		}

		for (auto* feature : features) {
			if (!feature)
				continue;

			const std::string shortName = feature->GetShortName();
			const auto baselineTotals =
				GetTimingTotalsForFeature(state.baseline, shortName);
			const auto currentTotals =
				GetTimingTotalsForFeature(current, shortName);
			FeatureHighlightDirection direction;
			if (baselineTotals.hasGpu && currentTotals.hasGpu) {
				direction.gpu = GetDirectionFromFrameTimeDelta(
					currentTotals.gpuAvgMs -
						baselineTotals.gpuAvgMs,
					baselineTotals.gpuAvgMs);
			}
			if (baselineTotals.hasCpu && currentTotals.hasCpu) {
				direction.cpu = GetDirectionFromFrameTimeDelta(
					currentTotals.cpuAvgMs -
						baselineTotals.cpuAvgMs,
					baselineTotals.cpuAvgMs);
			}
			if (direction.gpu != 0 || direction.cpu != 0)
				state.featureDirections[shortName] = direction;
		}
	}

	void RegisterSettingsEdit(
		TuningHighlightState& state,
		const ProfilingRenderer::PerformanceTimingSummary& timingBeforeEdit,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		if (!timingBeforeEdit.valid ||
			current.presentIntervalSampleId == 0) {
			state.pendingComparison = false;
			ClearHighlightDirections(state);
			return;
		}

		if (!state.pendingComparison)
			state.baseline = timingBeforeEdit;
		state.pendingComparison = true;
		state.lastEditSampleId = current.presentIntervalSampleId;
		const uint64_t remaining =
			std::numeric_limits<uint64_t>::max() -
			current.presentIntervalSampleId;
		state.measureAfterSampleId =
			current.presentIntervalSampleId +
			std::min(remaining, kTuningPostEditFreshSamples);
		state.settleAfterTime =
			currentTime + kTuningPostEditSettleSeconds;
		state.comparisonDeadline =
			currentTime + kTuningPostEditTimeoutSeconds;
		state.expireTime = 0.0;
		ClearHighlightDirections(state);
	}

	void UpdateHighlightState(
		TuningHighlightState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		const std::vector<Feature*>& features,
		double currentTime)
	{
		if (state.pendingComparison) {
			const bool timingReset =
				current.presentIntervalSampleId <
				state.lastEditSampleId;
			const bool timedOut =
				currentTime >= state.comparisonDeadline;
			if (timingReset || timedOut) {
				state.pendingComparison = false;
				ClearHighlightDirections(state);
			} else if (
				currentTime >= state.settleAfterTime &&
				current.presentIntervalSampleId >=
					state.measureAfterSampleId) {
				RecomputeHighlightDirections(
					state,
					current,
					features);
				state.pendingComparison = false;
				state.expireTime =
					currentTime + kTuningHighlightSeconds;
			}
		}

		if (!state.pendingComparison &&
			state.expireTime > 0.0 &&
			currentTime >= state.expireTime) {
			ClearHighlightDirections(state);
			state.expireTime = 0.0;
		}
	}

	int GetFeatureListDirection(
		const TuningHighlightState& state,
		const std::string& shortName)
	{
		const auto it = state.featureDirections.find(shortName);
		if (it == state.featureDirections.end())
			return 0;
		if (it->second.gpu > 0 || it->second.cpu > 0)
			return 1;
		if (it->second.gpu < 0 || it->second.cpu < 0)
			return -1;
		return 0;
	}

	ProfilingRenderer::PerformanceTimingHighlight BuildSelectedHighlight(
		const TuningHighlightState& state,
		const std::string& selectedShortName)
	{
		ProfilingRenderer::PerformanceTimingHighlight highlight;
		highlight.frameDirection = state.frameDirection;
		highlight.fpsDirection = state.fpsDirection;
		highlight.gpuTotalDirection = state.gpuTotalDirection;
		highlight.cpuTotalDirection = state.cpuTotalDirection;

		const auto it =
			state.featureDirections.find(selectedShortName);
		if (it != state.featureDirections.end()) {
			highlight.featureGpuDirection = it->second.gpu;
			highlight.featureCpuDirection = it->second.cpu;
		}
		return highlight;
	}

	void ClearFinishedFeatureCostMeasurement(
		FeatureCostMeasurementState& state)
	{
		if ((state.stage == FeatureCostMeasurementStage::Complete ||
				state.stage == FeatureCostMeasurementStage::Failed) &&
			!state.restorePending) {
			state = {};
		}
	}

	void InvalidateCompletedResultIfStale(
		FeatureCostMeasurementState& state,
		const json& currentUiState)
	{
		if (state.stage != FeatureCostMeasurementStage::Complete)
			return;

		if (!IsFeatureCostCadenceContextCurrent(state) ||
			!PerformanceTuning::AreJsonValuesEquivalent(
				currentUiState,
				state.resultSettingsState)) {
			state = {};
		}
	}
}

void PerformanceTuningRenderer::UpdateActiveMeasurements()
{
	if (!IsAnyFeatureCostMeasurementRunning())
		return;

	CaptureProfilerStateForPerformanceTuning();
	SyncProfilerCaptureModeLimit();
	const auto features = BuildPerformanceFeatureList();
	const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(
		BuildPerformanceFeaturePrefixes(features),
		Profiler::CaptureMode::WholeFrameOnly);
	const double currentTime = ImGui::GetTime();
	UpdateRunningFeatureCostMeasurements(timing, currentTime);
	SyncProfilerCaptureModeLimit();
	g_lastTimingSummary = timing;
}

void PerformanceTuningRenderer::RenderMeasurementOverlay()
{
	auto* feature = FindRunningFeature();
	if (!feature)
		return;

	auto& state = g_costMeasurementStates[feature->GetShortName()];
	const auto* viewport = ImGui::GetMainViewport();
	const float margin = 24.0f * Util::GetUIScale();
	ImGui::SetNextWindowPos(
		ImVec2(
			viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
			viewport->WorkPos.y + margin),
		ImGuiCond_Always,
		ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.88f);
	constexpr ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##PerformanceTuningMeasurementOverlay", nullptr, flags)) {
		auto featureName = feature->GetDisplayName();
		const auto title = std::vformat(
			T(TKEY("overlay.measuring"), "Measuring: {}"),
			std::make_format_args(featureName));
		ImGui::TextUnformatted(title.c_str());
		const auto progress =
			BuildFeatureCostProgressText(state, ImGui::GetTime());
		if (!progress.empty())
			ImGui::TextDisabled("%s", progress.c_str());
	}
	ImGui::End();
}

void PerformanceTuningRenderer::Render()
{
	CaptureProfilerStateForPerformanceTuning();

	auto features = BuildPerformanceFeatureList();
	const std::string lockedShortName = FindLockedFeatureShortName();
	if (!lockedShortName.empty()) {
		g_selectedShortName = lockedShortName;
		if (auto* lockedFeature = FindFeatureByShortName(lockedShortName);
			lockedFeature &&
			std::ranges::find(features, lockedFeature) == features.end()) {
			features.insert(features.begin(), lockedFeature);
		}
	}
	auto* selectedFeature = FindSelectedFeature(features);

	const bool measurementWasRunning =
		IsAnyFeatureCostMeasurementRunning();
	SyncProfilerCaptureModeLimit();
	const auto captureMode = measurementWasRunning ?
	                             Profiler::CaptureMode::WholeFrameOnly :
	                             Profiler::CaptureMode::DetailedPasses;
	const auto featurePrefixes =
		BuildPerformanceFeaturePrefixes(features);
	const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(
		featurePrefixes,
		captureMode);
	const double currentTime = ImGui::GetTime();

	UpdateRunningFeatureCostMeasurements(timing, currentTime);
	SyncProfilerCaptureModeLimit();

	if (!measurementWasRunning &&
		!IsAnyFeatureCostMeasurementRunning()) {
		UpdateHighlightState(
			g_highlightState,
			timing,
			features,
			currentTime);
	}

	if (!selectedFeature) {
		ImGui::TextDisabled(
			"%s",
			T(
				TKEY("empty"),
				"No loaded performance settings are available."));
		g_lastTimingSummary = timing;
		return;
	}

	RenderTopPerformanceCounters(timing, g_highlightState);
	ImGui::Spacing();

	// Keep UI work constant and minimal during both measurement captures. In
	// particular,
	// do not draw settings, serialize feature JSON, or request detailed pass
	// scopes while a measurement frame is being consumed.
	if (measurementWasRunning) {
		auto* runningFeature = FindRunningFeature();
		if (!runningFeature)
			runningFeature = selectedFeature;
		if (runningFeature) {
			ImGui::SeparatorText(
				runningFeature->GetDisplayName().c_str());
			(void)RenderFeatureCostMeasurement(
				runningFeature,
				g_costMeasurementStates[runningFeature->GetShortName()],
				currentTime);
		}
		SyncProfilerCaptureModeLimit();
		g_lastTimingSummary = timing;
		return;
	}

	const bool anyMeasurementLocked =
		IsAnyFeatureCostMeasurementLocked();
	const float selectorWidth = std::max(
		180.0f * Util::GetUIScale(),
		ImGui::GetContentRegionAvail().x * 0.18f);

	if (ImGui::BeginTable(
			"##PerformanceTuningLayout",
			3,
			ImGuiTableFlags_Resizable |
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn(
			"##PerformanceFeatureSelector",
			ImGuiTableColumnFlags_WidthFixed,
			selectorWidth);
		ImGui::TableSetupColumn(
			"##PerformanceSettings",
			ImGuiTableColumnFlags_WidthStretch,
			1.25f);
		ImGui::TableSetupColumn(
			"##PerformanceProfile",
			ImGuiTableColumnFlags_WidthStretch,
			1.0f);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild(
				"##PerformanceFeatureSelectorChild",
				ImVec2(0, 0),
				false)) {
			ImGui::BeginDisabled(anyMeasurementLocked);
			for (auto* feature : features) {
				if (!feature)
					continue;

				const bool selected =
					feature->GetShortName() ==
					g_selectedShortName;
				const int direction = GetFeatureListDirection(
					g_highlightState,
					feature->GetShortName());
				if (direction != 0)
					ImGui::PushStyleColor(
						ImGuiCol_Text,
						Util::Color::PerformanceDelta(direction));
				if (ImGui::Selectable(
						feature->GetDisplayName().c_str(),
						selected,
						ImGuiSelectableFlags_None)) {
					g_selectedShortName =
						feature->GetShortName();
					selectedFeature = feature;
				}
				if (direction != 0)
					ImGui::PopStyleColor();
			}
			ImGui::EndDisabled();
			if (anyMeasurementLocked) {
				ImGui::Spacing();
				ImGui::TextDisabled(
					"%s",
					T(
						TKEY("selection_locked"),
						"Feature selection is locked while a cost test or settings restoration is active."));
			}
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		if (ImGui::BeginChild(
				"##PerformanceSettingsChild",
				ImVec2(0, 0),
				false)) {
			ImGui::SeparatorText(
				selectedFeature->GetDisplayName().c_str());
			Util::PerformanceFrameStyleWrapper performanceStyle(true);
			auto& selectedCostState =
				g_costMeasurementStates[selectedFeature->GetShortName()];

			json currentUiState;
			bool capturedUiState = false;
			try {
				currentUiState =
					CapturePerformanceUiState(selectedFeature);
				capturedUiState = true;
			} catch (const std::exception& e) {
				logger::warn(
					"Failed to capture Performance Tuning UI state for {}: {}",
					selectedFeature->GetDisplayName(),
					e.what());
			} catch (...) {
				logger::warn(
					"Failed to capture Performance Tuning UI state for {}",
					selectedFeature->GetDisplayName());
			}

			if (capturedUiState) {
				const std::string shortName =
					selectedFeature->GetShortName();
				const auto lastIt =
					g_lastPerformanceUiStates.find(shortName);
				if (lastIt !=
						g_lastPerformanceUiStates.end() &&
					!PerformanceTuning::AreJsonValuesEquivalent(
						lastIt->second,
						currentUiState)) {
					RegisterSettingsEdit(
						g_highlightState,
						g_lastTimingSummary,
						timing,
						currentTime);
					ClearFinishedFeatureCostMeasurement(
						selectedCostState);
				}
				g_lastPerformanceUiStates[shortName] =
					currentUiState;
				InvalidateCompletedResultIfStale(
					selectedCostState,
					currentUiState);
			}

			const bool sceneControlled =
				IsFeatureControlledBySceneSettings(selectedFeature);
			const bool settingsDisabled =
				anyMeasurementLocked ||
				sceneControlled;
			ImGui::BeginDisabled(settingsDisabled);
			selectedFeature->DrawPerformanceSettings(true);
			ImGui::EndDisabled();

			if (sceneControlled) {
				Util::Text::WrappedWarning(
					T(
						TKEY("scene_settings_owned"),
						"Scene-specific settings currently control this feature. Pause those overrides before tuning it."));
			}

			const bool startMeasurement = RenderFeatureCostMeasurement(
				selectedFeature,
				selectedCostState,
				currentTime);
			const bool settingsRestored =
				RenderPerformanceUserDefaultButtons(
					selectedFeature,
					settingsDisabled);
			if (settingsRestored) {
				RegisterSettingsEdit(
					g_highlightState,
					timing,
					timing,
					currentTime);
				if (!selectedCostState.restorePending)
					selectedCostState = {};
				g_lastPerformanceUiStates.erase(
					selectedFeature->GetShortName());
			}
			if (startMeasurement && !settingsRestored) {
				// The post-edit preview is informational and uses a separate rolling
				// comparison. Cost measurement owns its raw sample windows, so discard
				// stale preview data before starting a capture.
				g_highlightState = {};
				StartFeatureCostMeasurement(
					selectedFeature,
					selectedCostState,
					timing,
					currentTime);
				SyncProfilerCaptureModeLimit();
			}
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(2);
		if (ImGui::BeginChild(
				"##PerformanceProfileChild",
				ImVec2(0, 0),
				false)) {
			ImGui::SeparatorText(
				T(TKEY("profiling"), "Profiling"));
			const auto selectedHighlight =
				BuildSelectedHighlight(
					g_highlightState,
					selectedFeature->GetShortName());
			ProfilingRenderer::RenderFeaturePerformanceSummary(
				selectedFeature->GetShortName(),
				&selectedHighlight);
		}
		ImGui::EndChild();
		ImGui::EndTable();
	}

	g_lastTimingSummary = timing;
}

void PerformanceTuningRenderer::CancelActiveMeasurements(CancelMode mode)
{
	for (auto& [shortName, state] : g_costMeasurementStates) {
		auto* feature = FindFeatureByShortName(shortName);
		if (IsFeatureCostMeasurementRunning(state)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(TKEY("error.cancelled"), "Measurement cancelled."));
		} else if (state.restorePending) {
			TryRestoreFeatureCostOriginalState(feature, state);
		}
	}

	if (mode == CancelMode::ClearSession) {
		std::erase_if(
			g_costMeasurementStates,
			[](const auto& entry) {
				return !entry.second.restorePending;
			});
		g_performanceDefaultsMessages.clear();
		g_lastPerformanceUiStates.clear();
		g_selectedShortName.clear();
		g_highlightState = {};
		g_lastTimingSummary = {};
	}
	SyncProfilerCaptureModeLimit();
	RestoreProfilerStateAfterPerformanceTuning();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return IsAnyFeatureCostMeasurementLocked();
}

bool PerformanceTuningRenderer::PrepareForSceneUpdate()
{
	return PrepareFeatureCostMeasurementsForSceneChange(
		false,
		T(TKEY("error.scene_changed"), "Stopped because the scene changed."));
}

bool PerformanceTuningRenderer::PrepareForSceneSettingsTransition()
{
	return PrepareFeatureCostMeasurementsForSceneChange(
		true,
		T(
			TKEY("error.scene_owned"),
			"Stopped because scene-specific settings took control of this feature."));
}

#undef I18N_KEY_PREFIX
