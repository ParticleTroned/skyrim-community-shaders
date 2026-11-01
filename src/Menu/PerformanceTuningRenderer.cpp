#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <span>
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

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/PerformanceTuningJson.h"
#include "Menu/PerformanceQuickScan.h"
#include "Menu/PerformanceTuningController.h"
#include "Menu/PerformanceTuningStatistics.h"
#include "Menu/SettingsPage.h"
#include "Utils/FeatureProfiling.h"
#include "Utils/RuntimeToggle.h"
#include "Menu/PerformanceTuningMeasurement.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "Utils/VanityCamera.h"

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
	constexpr double kFeatureCostTransitionTimeoutSeconds = 15.0;
	constexpr double kFeatureCostSampleProgressTimeoutSeconds = 3.0;
	constexpr double kFeatureCostProfilerDrainTimeoutSeconds = 1.25;
	constexpr double kFeatureCostOverallTimeoutSeconds = PerformanceTuning::kMaximumRunSeconds;
	constexpr double kFeatureCostMeasurementMaximumSeconds = kFeatureCostMeasurementSeconds + 1.0;
	constexpr uint64_t kFeatureCostPipelineDrainPresentCount = Profiler::kFrameLatency + 2;
	constexpr double kFeatureCostCameraPositionThreshold = 32.0;
	constexpr double kFeatureCostCameraBasisThreshold = 0.05;
	constexpr double kFeatureCostCameraProjectionThreshold = 0.01;
	constexpr double kFeatureCostWeatherTransitionThreshold = 0.10;
	static_assert(
		PerformanceTuning::kMaximumPresentIntervalMs ==
			Profiler::kMaxPresentIntervalMs,
		"Profiler and measurement interruption cutoffs must match");

	struct SceneFingerprint
	{
		const void* cell = nullptr;
		const void* currentWeather = nullptr;
		float weatherTransition = 0.0f;
		bool interior = false;
		std::array<float, 3> cameraPosition{};
		std::array<float, 9> cameraBasis{};
		std::array<float, 4> cameraProjection{};
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
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
		CurrentBefore,
		Comparison
	};

	enum class FeatureCostMeasurementStage
	{
		Idle,
		AwaitingMenuClose,
		Settling,
		Measuring,
		Draining,
		Restoring,
		Complete,
		Failed
	};

	enum class FeatureCostTransitionWait
	{
		FeatureReady,
		SettleDelay,
		FreshPresents,
		PostChangeSoak
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
		FeatureCostMeasurementLeg leg = FeatureCostMeasurementLeg::CurrentBefore;
		json originalState;
		json measurementCurrentState;
		json comparisonState;
		json runSettingsState;
		json resultSettingsState;
		Upscaling::PerformanceMeasurementFrameGenerationSettings
			originalFrameGenerationSettings;
		SceneFingerprint runScene;
		SceneFingerprint resultScene;
		bool hasResultScene = false;
		bool featureStateRestorePending = false;
		bool frameGenerationOverrideApplied = false;
		bool frameGenerationTemporarilyDisabled = false;
		bool instrumentationLimited = false;
		bool restorePending = false;
		bool devBenchOwned = false;
		double overallStartTime = 0.0;
		double stageStartTime = 0.0;
		double lastProgressTime = 0.0;
		uint64_t lastObservedPresentSampleId = 0;
		uint64_t timingDiscontinuityEpoch = 0;
		uint64_t profilerSkippedCaptureStart = 0;
		PerformanceTuning::TransitionGateState transitionGate;
		FeatureCostTransitionWait transitionWait =
			FeatureCostTransitionWait::FeatureReady;
		PerformanceTuning::SampleWindow currentBefore;
		PerformanceTuning::SampleWindow comparison;
		PerformanceTuning::CostResult result;
		std::string failureReason;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static bool g_closedMenuMeasurement = false;
	static bool g_reopenMenuAfterMeasurement = false;
	static double g_restartAllowedTime = 0.0;
	static double g_nextRestoreRetry = 0.0;
	static Util::VanityCameraSuppressionLease g_vanityCameraSuppression;
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

	struct FeatureCostBatchState
	{
		bool active = false;
		bool devBenchOwned = false;
		bool reopenMenuOnCompletion = false;
		std::size_t nextFeatureIndex = 0;
		RE::FormID cellFormId = 0;
		std::vector<std::string> features;
		std::string failureMessage;
	};

	static std::unordered_map<std::string, std::optional<PerformanceTuningController::DisabledConfiguration>> g_disabledFeatureConfigurations;
	static FeatureCostBatchState g_featureCostBatch;
	static std::string g_featureCostUiMessage;

	struct QuickScanRow
	{
		std::string feature;
		std::string label;
		std::string coverage;
		std::optional<double> gpuMs;
		std::optional<double> cpuMs;
	};
	struct QuickScanState
	{
		PerformanceQuickScan::Controller controller;
		bool devBenchOwned = false;
		bool reopenMenu = false;
		PerformanceQuickScan::CaptureOwnership<Profiler> capture;
		RE::FormID cell = 0;
		std::uint64_t captureId = 0;
		std::uint32_t resolvedFrames = 0;
		std::uint32_t invalidGpuFrames = 0;
		std::uint32_t invalidCpuFrames = 0;
		std::uint32_t gpuRefusals = 0;
		std::uint32_t cpuRefusals = 0;
		double captureSeconds = 0.0;
		double captureStarted = 0.0;
		json settings;
		std::vector<QuickScanRow> rows;
		std::string failure;
	};
	static QuickScanState g_quickScan;

	static_assert(PerformanceQuickScan::kCaptureFrames <= Profiler::kHistorySize);
	enum class MonoSweepPhase { Idle, Staging, Measuring, Cooldown, Restoring, Complete, Failed, Cancelled };
	struct MonoSweepCase
	{
		std::string id;
		std::string label;
		Upscaling::MonoProfile profile;
		PerformanceTuning::CostResult result;
		bool measured = false;
	};
	struct MonoSweepState
	{
		MonoSweepPhase phase = MonoSweepPhase::Idle;
		MonoSweepPhase terminalPhase = MonoSweepPhase::Complete;
		bool devBenchOwned = false;
		bool restorePending = false;
		bool reopenMenu = false;
		std::string matrix;
		std::string failure;
		json originalState;
		Upscaling::MonoProfile originalProfile;
		Upscaling::PerformanceMeasurementFrameGenerationSettings originalFrameGeneration;
		SceneFingerprint scene;
		std::vector<MonoSweepCase> cases;
		std::size_t index = 0;
		double phaseStarted = 0.0;
		double lastRestoreAttempt = -1.0;
		bool Active() const
		{
			return restorePending || phase == MonoSweepPhase::Staging || phase == MonoSweepPhase::Measuring ||
				phase == MonoSweepPhase::Cooldown || phase == MonoSweepPhase::Restoring;
		}
	};
	static MonoSweepState g_monoSweep;
	void UpdateMonoSweep(double now);
	void BeginMonoSweepRestore(MonoSweepPhase terminal, std::string failure = {});
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct MeasurementTrace
	{
		uint64_t sequence = 0;
		double elapsedMs = 0.0;
		uint32_t frame = 0;
		std::string feature;
		std::string caseId;
		ProfilingRenderer::PerformanceTimingSummary timing;
	};
	static std::deque<MeasurementTrace> g_measurementTrace;
	static uint64_t g_traceNextSequence = 1;
	static double g_traceStarted = 0.0;
	static double g_traceLast = -1.0;
	void RecordMeasurementTrace(const ProfilingRenderer::PerformanceTimingSummary& timing, double now);
#endif

	void SyncFeatureCostVanityCameraSuppression();
	void FinishQuickScan(PerformanceQuickScan::Phase phase, std::string failure = {}, bool reopen = true);
	bool UpdateFeatureCostBatch(double currentTime);
	void RenderQuickScan();
	void RenderFeatureSetMeasurement(const std::vector<Feature*>& features);
	const char* GetFeatureToggleBlockReason(Feature* feature);


	SceneFingerprint CaptureSceneFingerprint()
	{
		const auto* player = globals::game::player;
		const auto* cell = player ? player->parentCell : nullptr;
		const auto* sky = globals::game::sky;
		SceneFingerprint result;
		result.cell = cell;
		result.currentWeather = sky ? sky->currentWeather : nullptr;
		result.weatherTransition = sky ? sky->currentWeatherPct : 0.0f;
		result.interior = cell && cell->IsInteriorCell();

		if (globals::game::shadowState) {
			const auto eye = Util::GetEyePosition();
			result.cameraPosition = { eye.x, eye.y, eye.z };
		}
		const auto& view = globals::game::frameBufferCached.GetCameraView();
		result.cameraBasis = {
			view._11, view._12, view._13,
			view._21, view._22, view._23,
			view._31, view._32, view._33
		};
		const auto& projection =
			globals::game::frameBufferCached.GetCameraProjUnjittered();
		result.cameraProjection = {
			projection._11,
			projection._22,
			projection._33,
			projection._43
		};

		if (globals::d3d::swapChain) {
			DXGI_SWAP_CHAIN_DESC description{};
			if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&description))) {
				result.outputWidth = description.BufferDesc.Width;
				result.outputHeight = description.BufferDesc.Height;
			}
		}
		return result;
	}

	bool HasMaterialSceneChange(
		const SceneFingerprint& baseline,
		const SceneFingerprint& current)
	{
		if (baseline.cell != current.cell ||
			baseline.currentWeather != current.currentWeather ||
			baseline.interior != current.interior ||
			baseline.outputWidth != current.outputWidth ||
			baseline.outputHeight != current.outputHeight ||
			std::abs(
				static_cast<double>(current.weatherTransition) -
				static_cast<double>(baseline.weatherTransition)) >
				kFeatureCostWeatherTransitionThreshold) {
			return true;
		}

		double positionDistanceSquared = 0.0;
		for (size_t index = 0; index < baseline.cameraPosition.size(); ++index) {
			const double difference =
				static_cast<double>(current.cameraPosition[index]) -
				static_cast<double>(baseline.cameraPosition[index]);
			positionDistanceSquared += difference * difference;
		}
		if (positionDistanceSquared >
			kFeatureCostCameraPositionThreshold *
				kFeatureCostCameraPositionThreshold) {
			return true;
		}

		for (size_t index = 0; index < baseline.cameraBasis.size(); ++index) {
			if (std::abs(
					static_cast<double>(current.cameraBasis[index]) -
					static_cast<double>(baseline.cameraBasis[index])) >
				kFeatureCostCameraBasisThreshold) {
				return true;
			}
		}
		for (size_t index = 0;
			index < baseline.cameraProjection.size();
			++index) {
			if (std::abs(
					static_cast<double>(current.cameraProjection[index]) -
					static_cast<double>(baseline.cameraProjection[index])) >
				kFeatureCostCameraProjectionThreshold) {
				return true;
			}
		}
		return false;
	}

	bool IsFeatureCostEnvironmentEligible()
	{
		if (!globals::state || !globals::state->inWorld)
			return false;
		// A second configuration owner or disabled CS would invalidate both captures.
		if (!globals::shaderCache || !globals::shaderCache->IsEnabled() ||
			ABTestingManager::GetSingleton()->IsEnabled())
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
		return state.stage == FeatureCostMeasurementStage::AwaitingMenuClose ||
		       state.stage == FeatureCostMeasurementStage::Settling ||
		       state.stage == FeatureCostMeasurementStage::Measuring ||
		       state.stage == FeatureCostMeasurementStage::Draining ||
		       state.stage == FeatureCostMeasurementStage::Restoring;
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
		case FeatureCostMeasurementLeg::CurrentBefore:
			return state.currentBefore;
		case FeatureCostMeasurementLeg::Comparison:
			return state.comparison;
		default:
			return state.currentBefore;
		}
	}

	const PerformanceTuning::SampleWindow& GetFeatureCostSampleWindow(
		const FeatureCostMeasurementState& state)
	{
		switch (state.leg) {
		case FeatureCostMeasurementLeg::CurrentBefore:
			return state.currentBefore;
		case FeatureCostMeasurementLeg::Comparison:
			return state.comparison;
		default:
			return state.currentBefore;
		}
	}

	const char* GetFeatureCostLegStatusLabel(const FeatureCostMeasurementState& state)
	{
		switch (state.leg) {
		case FeatureCostMeasurementLeg::CurrentBefore:
			return T(TKEY("status.current_before"), "current");
		case FeatureCostMeasurementLeg::Comparison:
			return T(TKEY("status.comparison"), "comparison");
		default:
			return T(TKEY("status.current_before"), "current");
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
			!state.frameGenerationOverrideApplied)
			return true;

		bool featureRestored = !state.featureStateRestorePending;
		if (state.featureStateRestorePending && feature) {
			try {
				feature->RestorePerformanceCostMeasurementState(state.originalState);
				if (!PerformanceTuning::AreJsonValuesEquivalent(
						feature->CapturePerformanceCostMeasurementState(),
						state.originalState)) {
					throw std::runtime_error(
						"restored state did not match its snapshot");
				}
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
			// can be restored or retried. Clear only that pending resource; the
			// independent global frame-generation restoration still runs below.
			logger::error(
				"Could not restore a removed feature after Performance Tuning");
			state.featureStateRestorePending = false;
			featureRestored = true;
		}

		bool frameGenerationRestored = !state.frameGenerationOverrideApplied;
		if (state.frameGenerationOverrideApplied) {
			try {
				auto& upscaling = globals::features::upscaling;
				upscaling.RestoreFrameGenerationSettingsAfterPerformanceMeasurement(
					state.originalFrameGenerationSettings);
				const auto restoredFrameGeneration =
					upscaling.CaptureFrameGenerationSettingsForPerformanceMeasurement();
				if (restoredFrameGeneration.mode !=
						state.originalFrameGenerationSettings.mode ||
					restoredFrameGeneration.forceEnable !=
						state.originalFrameGenerationSettings.forceEnable ||
					restoredFrameGeneration.allowInMenus !=
						state.originalFrameGenerationSettings.allowInMenus) {
					throw std::runtime_error(
						"frame-generation settings did not match their snapshot");
				}
				state.frameGenerationOverrideApplied = false;
				frameGenerationRestored = true;
			} catch (const std::exception& e) {
				logger::error(
					"Failed to restore frame generation after Performance Tuning: {}",
					e.what());
			} catch (...) {
				logger::error(
					"Failed to restore frame generation after Performance Tuning");
			}
		}

		state.restorePending =
			state.featureStateRestorePending ||
			state.frameGenerationOverrideApplied;
		return featureRestored && frameGenerationRestored &&
		       !state.restorePending;
	}

	bool IsFeatureCostExpectedEnabledState(
		Feature* feature,
		const FeatureCostMeasurementState& state)
	{
		if (!feature)
			return false;

		const bool comparisonLeg =
			state.leg == FeatureCostMeasurementLeg::Comparison;
		const bool expectedEnabled = !comparisonLeg;
		return feature->IsPerformanceCostMeasurementEnabled() ==
		       expectedEnabled;
	}

	bool IsFeatureCostExpectedSerializedState(
		Feature* feature,
		const FeatureCostMeasurementState& state)
	{
		if (!IsFeatureCostExpectedEnabledState(feature, state))
			return false;

		try {
			const bool comparisonLeg =
				state.leg == FeatureCostMeasurementLeg::Comparison;
			const auto& expectedState = comparisonLeg ?
			                                state.comparisonState :
			                                state.measurementCurrentState;
			return PerformanceTuning::AreJsonValuesEquivalent(
				feature->CapturePerformanceCostMeasurementState(),
				expectedState);
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to validate the Performance Tuning leg state for {}: {}",
				feature->GetDisplayName(),
				e.what());
		} catch (...) {
			logger::warn(
				"Failed to validate the Performance Tuning leg state for {}",
				feature->GetDisplayName());
		}
		return false;
	}

	void FailFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		std::string reason)
	{
		const bool restored = TryRestoreFeatureCostOriginalState(feature, state);
		state.stage = FeatureCostMeasurementStage::Failed;
		g_restartAllowedTime = ImGui::GetTime() + PerformanceTuning::kRestartCooldownSeconds;
		g_vanityCameraSuppression.Release();
		state.failureReason = std::move(reason);
		if (!restored) {
			if (!state.failureReason.empty())
				state.failureReason += " ";
			state.failureReason += T(
				TKEY("error.restore_pending"),
				"Original settings could not be restored; retry restoration.");
		}
	}

	bool PrepareFeatureCostMeasurementsForSceneChange(
		bool cancelAllRunning,
		const char* failureReason)
	{
		const auto currentScene = CaptureSceneFingerprint();
		bool allRestored = true;
		for (auto& [shortName, state] : g_costMeasurementStates) {
			auto* feature = FindFeatureByShortName(shortName);
			if (IsFeatureCostMeasurementRunning(state) &&
				(cancelAllRunning ||
					HasMaterialSceneChange(state.runScene, currentScene))) {
				FailFeatureCostMeasurement(
					feature,
					state,
					failureReason ? failureReason :
									T(TKEY("error.scene_changed"), "Stopped because the scene changed."));
			} else if (state.restorePending) {
				TryRestoreFeatureCostOriginalState(feature, state);
			}
			allRestored &= !state.restorePending;
		}

		SyncProfilerCaptureModeLimit();
		return allRestored;
	}

	void BeginFeatureCostSettling(
		FeatureCostMeasurementState& state,
		FeatureCostMeasurementLeg leg,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		state.leg = leg;
		state.stage = FeatureCostMeasurementStage::Settling;
		state.stageStartTime = currentTime;
		state.lastProgressTime = currentTime;
		state.lastObservedPresentSampleId = summary.presentIntervalSampleId;
		state.transitionGate = {};
		state.transitionWait = FeatureCostTransitionWait::FeatureReady;
	}

	void UpdateFeatureCostTransitionWait(
		FeatureCostMeasurementState& state,
		bool featureReady,
		double currentTime,
		uint64_t currentPresentSampleId,
		double minimumReadySeconds,
		uint64_t minimumFreshPresentCount)
	{
		if (!featureReady || !state.transitionGate.continuouslyReady) {
			state.transitionWait = FeatureCostTransitionWait::FeatureReady;
			return;
		}

		if (currentTime - state.transitionGate.readyStartTime <
			minimumReadySeconds) {
			state.transitionWait = FeatureCostTransitionWait::SettleDelay;
			return;
		}

		const uint64_t freshPresentCount =
			currentPresentSampleId >=
					state.transitionGate.readyStartPresentSampleId ?
				currentPresentSampleId -
					state.transitionGate.readyStartPresentSampleId :
				0;
		state.transitionWait =
			freshPresentCount < minimumFreshPresentCount ?
				FeatureCostTransitionWait::FreshPresents :
				FeatureCostTransitionWait::PostChangeSoak;
	}

	const char* GetFeatureCostTransitionTimeoutReason(
		const FeatureCostMeasurementState& state)
	{
		switch (state.transitionWait) {
		case FeatureCostTransitionWait::FeatureReady:
			return T(
				TKEY("error.transition_ready_timeout"),
				"Stopped because the feature did not remain ready to measure.");
		case FeatureCostTransitionWait::SettleDelay:
			return T(
				TKEY("error.transition_delay_timeout"),
				"Stopped while waiting for the applied feature state.");
		case FeatureCostTransitionWait::FreshPresents:
			return T(
				TKEY("error.transition_presents_timeout"),
				"Stopped while waiting for fresh presented frames after the state change.");
		case FeatureCostTransitionWait::PostChangeSoak:
		default:
			return T(
				TKEY("error.transition_soak_timeout"),
				"Stopped while waiting for the post-change timing window to clear.");
		}
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
			if (feature->IsPerformanceCostMeasurementEnabled())
				throw std::runtime_error("comparison state remained enabled");
			state.comparisonState =
				feature->CapturePerformanceCostMeasurementState();
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
		double currentTime, bool allowClosedMenu = false, bool devBenchOwned = false)
	{
		if (!feature ||
			!feature->SupportsPerformanceCostMeasurement() ||
			!globals::profiler || !globals::profiler->IsInitialized() ||
			globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running ||
			g_quickScan.controller.Active() ||
			!feature->IsPerformanceCostMeasurementEnabled() ||
			!feature->IsPerformanceTuningApplicable() ||
			IsFeatureControlledBySceneSettings(feature) ||
			!IsFeatureCostEnvironmentEligible() ||
			!globals::menu || (!globals::menu->IsEnabled && !allowClosedMenu) ||
			EditorWindow::GetSingleton()->open ||
			IsAnyFeatureCostMeasurementLocked() ||
			currentTime < g_restartAllowedTime) {
			return;
		}

		CaptureProfilerStateForPerformanceTuning();
		const bool menuWasOpen = globals::menu->IsEnabled;
		state = {};
		state.devBenchOwned = devBenchOwned;
		try {
			state.originalState =
				feature->CapturePerformanceCostMeasurementState();
			state.runSettingsState =
				CapturePerformanceUiState(feature);
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

		try {
			auto& upscaling = globals::features::upscaling;
			state.originalFrameGenerationSettings =
				upscaling.CaptureFrameGenerationSettingsForPerformanceMeasurement();
			const auto frameGenerationStatus =
				upscaling.GetFrameGenerationStatusForPerformanceMeasurement();
			state.frameGenerationTemporarilyDisabled =
				frameGenerationStatus.configured ||
				frameGenerationStatus.requestedNow ||
				frameGenerationStatus.activeNow;
			upscaling.DisableFrameGenerationForPerformanceMeasurement();
			state.frameGenerationOverrideApplied = true;
			state.restorePending = true;
			state.measurementCurrentState =
				feature->CapturePerformanceCostMeasurementState();
			if (!feature->IsPerformanceCostMeasurementEnabled()) {
				throw std::runtime_error(
					"no non-frame-generated current state remains to measure");
			}
		} catch (const std::exception& e) {
			logger::warn(
				"Failed to prepare the frame-generation-free measurement state for {}: {}",
				feature->GetDisplayName(),
				e.what());
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.frame_generation_prepare"),
					"Could not prepare a measurable state with frame generation off."));
			return;
		} catch (...) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.frame_generation_prepare"),
					"Could not prepare a measurable state with frame generation off."));
			return;
		}

		if (!g_vanityCameraSuppression.Acquire()) {
			FailFeatureCostMeasurement(feature, state,
				T(TKEY("error.idle_camera"), "Could not suppress the automatic idle camera."));
			return;
		}
		state.runScene = CaptureSceneFingerprint();
		state.overallStartTime = currentTime;
		state.timingDiscontinuityEpoch =
			summary.presentDiscontinuityEpoch;
		BeginFeatureCostSettling(
			state,
			FeatureCostMeasurementLeg::CurrentBefore,
			summary,
			currentTime);
		if (menuWasOpen)
			state.stage = FeatureCostMeasurementStage::AwaitingMenuClose;
		if (!g_closedMenuMeasurement)
			g_reopenMenuAfterMeasurement = menuWasOpen;
		g_closedMenuMeasurement = true;
		g_lastTimingSummary = summary;
		if (menuWasOpen)
			globals::menu->CloseMenu();
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
		if (state.leg == FeatureCostMeasurementLeg::CurrentBefore) {
			if (!ApplyFeatureCostComparisonState(feature, state)) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.comparison_apply"),
						"Stopped because the comparison state could not be applied."));
				return false;
			}
			BeginFeatureCostSettling(
				state,
				FeatureCostMeasurementLeg::Comparison,
				summary,
				currentTime);
			return true;
		}

		if (!TryRestoreFeatureCostOriginalState(feature, state)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.restore_failed"),
					"Stopped because the original feature state could not be restored."));
			return false;
		}
		state.stage = FeatureCostMeasurementStage::Restoring;
		state.stageStartTime = currentTime;
		return true;
	}

	void FinishFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state, double currentTime)
	{
		state.result = PerformanceTuning::CalculateCostResult(state.currentBefore, state.comparison);
		try {
			state.resultSettingsState = CapturePerformanceUiState(feature);
			if (!PerformanceTuning::AreJsonValuesEquivalent(
					state.resultSettingsState,
					state.runSettingsState)) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.settings_changed"),
						"Stopped because performance settings changed during measurement."));
				return;
			}
		} catch (...) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.result_snapshot"),
					"Stopped because the result settings snapshot could not be captured."));
			return;
		}
		state.resultScene = CaptureSceneFingerprint();
		state.hasResultScene = true;
		state.stage = FeatureCostMeasurementStage::Complete;
		g_restartAllowedTime = currentTime + PerformanceTuning::kRestartCooldownSeconds;
		g_vanityCameraSuppression.Release();
	}

	void UpdateFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		if (!feature || !IsFeatureCostMeasurementRunning(state))
			return;

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
					"Stopped because the game was paused, loading, lost focus, CS was disabled, or A/B testing was enabled."));
			return;
		}
		if (state.stage == FeatureCostMeasurementStage::Restoring) {
			if (HasMaterialSceneChange(state.runScene, CaptureSceneFingerprint())) {
				FailFeatureCostMeasurement(feature, state,
					T(TKEY("error.scene_changed"), "Stopped because the scene changed."));
				return;
			}
			if (currentTime - state.stageStartTime >= PerformanceTuning::kRestoreWaitSeconds &&
				feature->IsPerformanceCostMeasurementReady())
				FinishFeatureCostMeasurement(feature, state, currentTime);
			return;
		}
		if (state.stage == FeatureCostMeasurementStage::AwaitingMenuClose)
			return;
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
		if (HasMaterialSceneChange(
				state.runScene,
				CaptureSceneFingerprint())) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(TKEY("error.scene_changed"), "Stopped because the scene changed."));
			return;
		}
		if (!feature->IsPerformanceTuningApplicable()) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.no_longer_applicable"),
					"Stopped because the feature is no longer active in this scene."));
			return;
		}
		if (IsFeatureControlledBySceneSettings(feature)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.scene_owned"),
					"Stopped because scene-specific settings took control of this feature."));
			return;
		}
		if (!IsFeatureCostExpectedEnabledState(feature, state)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.measurement_state_changed"),
					"Stopped because the measured feature state changed during capture."));
			return;
		}
		if (state.stage != FeatureCostMeasurementStage::Settling &&
			!globals::features::upscaling
				.IsFrameGenerationQuiescentForPerformanceMeasurement()) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.frame_generation_active"),
					"Stopped because frame generation was requested or active."));
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

		if (state.stage == FeatureCostMeasurementStage::Settling) {
			const bool targetEnabled =
				state.leg != FeatureCostMeasurementLeg::Comparison;
			const double settleSeconds = std::max(
				0.0,
				feature->GetPerformanceCostMeasurementSettleSeconds(
					targetEnabled));
			const uint64_t freshPresentCount = std::max(
				kFeatureCostPipelineDrainPresentCount,
				feature->GetPerformanceCostMeasurementFreshPresentCount(
					targetEnabled));
			const double postFreshSoakSeconds = std::max(
				0.0,
				feature->GetPerformanceCostMeasurementPostFreshSoakSeconds(
					targetEnabled));
			const bool featureReady =
				feature->IsPerformanceCostMeasurementReady() &&
				globals::features::upscaling
					.IsFrameGenerationQuiescentForPerformanceMeasurement();
			const auto transitionResult =
				PerformanceTuning::UpdateTransitionGate(
					state.transitionGate,
					featureReady,
					currentTime,
					summary.presentIntervalSampleId,
					settleSeconds,
					freshPresentCount,
					postFreshSoakSeconds);
			UpdateFeatureCostTransitionWait(
				state,
				featureReady,
				currentTime,
				summary.presentIntervalSampleId,
				settleSeconds,
				freshPresentCount);
			if (transitionResult ==
				PerformanceTuning::TransitionGateResult::TimingReset) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.timing_reset"),
						"Stopped because the frame timing source was reset."));
				return;
			}
			const double minimumWait = state.leg == FeatureCostMeasurementLeg::CurrentBefore ?
			                               PerformanceTuning::kInitialWaitSeconds :
			                               std::max(PerformanceTuning::kComparisonWaitSeconds, settleSeconds);
			if (transitionResult != PerformanceTuning::TransitionGateResult::Ready ||
				currentTime - state.stageStartTime < minimumWait) {
				if (currentTime - state.stageStartTime >
					kFeatureCostTransitionTimeoutSeconds) {
					FailFeatureCostMeasurement(
						feature,
						state,
						GetFeatureCostTransitionTimeoutReason(state));
				}
				return;
			}

			auto& window = GetFeatureCostSampleWindow(state);
			PerformanceTuning::BeginSampleWindow(
				window,
				summary.presentIntervalSampleId,
				summary.wholeFrameSampleId,
				currentTime,
				true);
			state.profilerSkippedCaptureStart =
				summary.skippedWholeFrameCaptureCount;
			state.stage = FeatureCostMeasurementStage::Measuring;
			state.stageStartTime = currentTime;
			state.lastProgressTime = currentTime;
			return;
		}

		auto& window = GetFeatureCostSampleWindow(state);
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
			if (!feature->IsPerformanceCostMeasurementReady() ||
				!globals::features::upscaling
					.IsFrameGenerationQuiescentForPerformanceMeasurement()) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.feature_unready"),
						"Stopped because the feature stopped reporting ready during measurement."));
				return;
			}
			if (currentTime - state.stageStartTime >
				kFeatureCostMeasurementMaximumSeconds) {
				FailFeatureCostMeasurement(
					feature,
					state,
					T(
						TKEY("error.insufficient_frames"),
						"Stopped because too few valid game frames were produced within six seconds."));
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
							"Stopped because too few valid game frames were produced within six seconds."));
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
						"Stopped because too few valid game frames were produced within six seconds."));
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
		// Full JSON validation is deliberately outside the measured Present range.
		// Per-frame capture uses only the cheap enabled-state check above so state
		// serialization cannot contaminate whole-frame CPU samples.
		if (!IsFeatureCostExpectedSerializedState(feature, state)) {
			FailFeatureCostMeasurement(
				feature,
				state,
				T(
					TKEY("error.measurement_state_changed"),
					"Stopped because the measured feature state changed during capture."));
			return;
		}

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
			state.instrumentationLimited = true;
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
		const PerformanceTuning::MetricDelta& metric,
		bool cadenceLimited = false)
	{
		if (!metric.IsReliable() || cadenceLimited)
			return 0;
		return metric.direction;
	}

	int GetDirectionFromFeatureCostFps(
		const PerformanceTuning::CostResult& result)
	{
		if (!result.fps.IsAvailable() ||
			!result.present.IsReliable() ||
			result.presentSynced || result.framePaced)
			return 0;
		return *result.fps.value < 0.0 ? 1 : -1;
	}

	bool HasUnreliableFeatureCostValue(
		const PerformanceTuning::CostResult& result)
	{
		const auto isUnreliable = [](const PerformanceTuning::MetricDelta& metric) {
			return metric.IsAvailable() && !metric.IsReliable();
		};
		return isUnreliable(result.present) ||
		       isUnreliable(result.wholeFrameGpu) ||
		       isUnreliable(result.wholeFrameCpu);
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

	void RenderCostMetric(
		const char* label,
		const PerformanceTuning::MetricDelta& metric,
		bool cadenceLimited = false)
	{
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine();
		if (!metric.IsAvailable()) {
			ImGui::TextDisabled(
				"%s",
				T(
					TKEY("result.unavailable_coverage"),
					"-- (insufficient matched samples)"));
			return;
		}

		const int direction =
			GetDirectionFromFeatureCostMetric(metric, cadenceLimited);
		if (direction != 0)
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				Util::Color::PerformanceDelta(direction));
		ImGui::Text(T("menu.performance_tuning.shared.3f_ms", "%+.3f ms"), *metric.valueMs);
		if (direction != 0)
			ImGui::PopStyleColor();

		const char* reliabilityText = nullptr;
		switch (metric.reliability) {
		case PerformanceTuning::MetricReliability::Reliable:
			reliabilityText = T(
				TKEY("result.reliable"),
				"Consistent direction in the captured blocks");
			break;
		case PerformanceTuning::MetricReliability::BelowPracticalFloor:
			reliabilityText = T(
				TKEY("result.below_floor"),
				"No material difference at this measurement scale");
			break;
		case PerformanceTuning::MetricReliability::DriftDominated:
			reliabilityText = T(
				TKEY("result.drift_dominated"),
				"Current-state drift is larger than the measured effect");
			break;
		case PerformanceTuning::MetricReliability::MixedBlockDirections:
			reliabilityText = T(
				TKEY("result.mixed"),
				"Mixed direction across captured blocks");
			break;
		case PerformanceTuning::MetricReliability::InsufficientBlockCoverage:
			reliabilityText = T(
				TKEY("result.block_coverage"),
				"Insufficient matched samples in one or more blocks");
			break;
		case PerformanceTuning::MetricReliability::Unavailable:
		default:
			break;
		}
		if (cadenceLimited) {
			reliabilityText = T(
				TKEY("result.cadence_limited"),
				"Observed cadence only; VSync or frame pacing prevents causal classification");
		}
		if (reliabilityText)
			ImGui::TextDisabled("%s", reliabilityText);

		if (metric.repeatability.median &&
			metric.repeatability.minimum &&
			metric.repeatability.maximum) {
			ImGui::TextDisabled(
				T(
					TKEY("result.repeatability_band_ms"),
					"Observed blocks: median %+.3f ms | range %+.3f to %+.3f | %u/%zu agree"),
				*metric.repeatability.median,
				*metric.repeatability.minimum,
				*metric.repeatability.maximum,
				metric.repeatability.agreeingBlockCount,
				PerformanceTuning::kMeasurementBlockCount);
		}
		if (metric.currentBeforeMeanMs && metric.comparisonMeanMs) {
			ImGui::TextDisabled(
				T(
					TKEY("result.window_means_ms"),
					"Captured means: %.3f ms current | %.3f ms comparison"),
				*metric.currentBeforeMeanMs,
				*metric.comparisonMeanMs);
		}
		if (metric.currentDriftMs && metric.practicalFloorMs) {
			ImGui::TextDisabled(
				T(
					TKEY("result.drift_floor_ms"),
					"Current-state drift: %+.3f ms | practical floor: %.3f ms"),
				*metric.currentDriftMs,
				*metric.practicalFloorMs);
		}
	}

	void RenderCostWindowDiagnostics(
		const char* label,
		const PerformanceTuning::WindowDiagnostics& diagnostics)
	{
		ImGui::TextDisabled(
			T(
				TKEY("result.window_diagnostics"),
				"%s: %.2f s, %u frames | GPU %u/%u | CPU %u/%u matched"),
			label,
			diagnostics.sampledDurationMs / 1000.0,
			diagnostics.presentSampleCount,
			diagnostics.wholeFrameGpu.validSampleCount,
			diagnostics.wholeFrameGpu.expectedSampleCount,
			diagnostics.wholeFrameCpu.validSampleCount,
			diagnostics.wholeFrameCpu.expectedSampleCount);
	}

	void RenderFeatureCostRunningStatus(
		Feature* feature,
		FeatureCostMeasurementState& state)
	{
		if (state.stage == FeatureCostMeasurementStage::Settling) {
			ImGui::TextDisabled(
				T(TKEY("status.settling"), "Settling %s"),
				GetFeatureCostLegStatusLabel(state));
			if (state.transitionWait ==
				FeatureCostTransitionWait::PostChangeSoak) {
				ImGui::SameLine();
				ImGui::TextDisabled(
					"%s",
					T(
						TKEY("status.post_change_soak"),
						"Waiting for the post-change timing window to clear"));
			} else if (state.transitionWait ==
					   FeatureCostTransitionWait::FreshPresents) {
				ImGui::SameLine();
				ImGui::TextDisabled(
					"%s",
					T(
						TKEY("status.fresh_presents"),
						"Flushing frame timings from before the state change"));
			} else if (state.transitionWait ==
					   FeatureCostTransitionWait::SettleDelay) {
				ImGui::SameLine();
				ImGui::TextDisabled(
					"%s",
					T(
						TKEY("status.settle_delay"),
						"Waiting for the state change to take effect"));
			} else if (feature) {
				ImGui::SameLine();
				ImGui::TextDisabled(
					"%s",
					feature->GetPerformanceCostMeasurementWaitText());
			}
		} else if (state.stage == FeatureCostMeasurementStage::Measuring) {
			const auto& sample = GetFeatureCostSampleWindow(state);
			ImGui::TextDisabled(
				T(TKEY("status.measuring"), "Measuring %s %.1f / %.1f s"),
				GetFeatureCostLegStatusLabel(state),
				std::clamp(
					sample.sampledDurationMs / 1000.0,
					0.0,
					kFeatureCostMeasurementSeconds),
				kFeatureCostMeasurementSeconds);
		} else if (state.stage == FeatureCostMeasurementStage::Draining) {
			ImGui::TextDisabled(
				T(
					TKEY("status.draining"),
					"Finalizing delayed whole-frame timings for %s"),
				GetFeatureCostLegStatusLabel(state));
		}

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
		FeatureCostMeasurementState& state)
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
				const char* reason =
					feature->GetPerformanceCostMeasurementUnavailableReason();
				ImGui::TextWrapped(
					"%s",
					reason ?
						reason :
						T(
							TKEY("cost.unavailable_reason"),
							"This feature has no runtime inactive state to compare in the current scene."));
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

		const bool anyMeasurementLocked = PerformanceTuningRenderer::HasActiveMeasurements() ||
			(globals::profiler && globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running);
		const bool applicable =
			feature->IsPerformanceTuningApplicable();
		const bool sceneControlled =
			IsFeatureControlledBySceneSettings(feature);
		const bool environmentEligible =
			IsFeatureCostEnvironmentEligible();
		const bool canStart =
			feature->SupportsPerformanceCostMeasurement() &&
			feature->IsPerformanceCostMeasurementEnabled() &&
			applicable &&
			!sceneControlled &&
			environmentEligible &&
			!anyMeasurementLocked &&
			ImGui::GetTime() >= g_restartAllowedTime;

		ImGui::BeginDisabled(!canStart);
		const bool startClicked = ImGui::Button(
			T(TKEY("cost.start"), "Measure actual feature cost"));
		ImGui::EndDisabled();
		if (!environmentEligible) {
			ImGui::TextWrapped("%s", T(TKEY("cost.environment_required"),
										 "Keep the game active and unpaused, enable Community Shaders, and disable A/B testing before measuring."));
		}
		if (ImGui::GetTime() < g_restartAllowedTime) {
			ImGui::SameLine();
			ImGui::TextDisabled(T(TKEY("status.cooldown"), "Ready again in %.0f seconds"),
				std::ceil(g_restartAllowedTime - ImGui::GetTime()));
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			const auto config = feature->GetPerformanceTuningConfig();
			ImGui::TextWrapped(
				T(
					TKEY("cost.tooltip"),
					"Closes the menu, waits 10 seconds, captures the current state for %.0f seconds, waits 10 seconds after switching to the comparison, captures for the same duration, then restores for at least 1 second. The complete run has a 45-second hard deadline."),
				kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("cost.tooltip_stability"),
					"Each capture contains five one-second blocks. Readiness and fresh-frame checks can extend a wait; delayed GPU/CPU timings remain matched to the captured frames."));
			ImGui::TextWrapped(
				T(TKEY("cost.comparison"), "Comparison: %s - %s"),
				config.comparisonLabel.data(),
				config.comparisonDetails.data());
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("cost.tooltip_scene"),
					"Keep the camera and scene reasonably still. Pause, loading, focus loss, a timing interruption, material camera movement, or frame generation stops the run; ordinary animation remains part of the average."));
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("cost.tooltip_frame_generation"),
					"Frame generation is temporarily forced off for every leg and restored afterward, including on cancel or failure."));
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
		const auto config = feature->GetPerformanceTuningConfig();
		ImGui::TextDisabled(
			T(
				TKEY("result.heading"),
				"Difference: current state - %s"),
			config.comparisonLabel.data());
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped(
				"%s",
				T(
					TKEY("result.tooltip"),
					"The primary value is the difference between the two full-window arithmetic means. Five matched time blocks show repeatability, not a confidence interval. There is no third capture or drift correction: keep the scene still. Negative frame time means the current state is faster. FPS is secondary."));
		}
		const bool cadenceLimited =
			state.result.presentSynced || state.result.framePaced;
		RenderCostMetric(
			T(TKEY("result.total"), "Game frame"),
			state.result.present,
			cadenceLimited);
		RenderCostMetric(
			T(TKEY("result.gpu"), "Whole-frame GPU"),
			state.result.wholeFrameGpu);
		RenderCostMetric(
			T(TKEY("result.cpu"), "Whole-frame CPU"),
			state.result.wholeFrameCpu);

		if (state.result.fps.IsAvailable()) {
			const int direction = GetDirectionFromFeatureCostFps(state.result);
			ImGui::TextDisabled(
				"%s",
				T(TKEY("result.fps"), "Game FPS"));
			ImGui::SameLine();
			if (direction != 0)
				ImGui::PushStyleColor(
					ImGuiCol_Text,
					Util::Color::PerformanceDelta(direction));
			ImGui::Text("%+.1f", *state.result.fps.value);
			if (direction != 0)
				ImGui::PopStyleColor();
			ImGui::TextDisabled(
				"%s",
				cadenceLimited ?
					T(
						TKEY("result.fps_cadence_limited"),
						"Secondary observed cadence; classification is disabled while paced") :
					T(
						TKEY("result.fps_secondary"),
						"Secondary metric; reliability follows total frame time"));
			if (state.result.fps.repeatability.median &&
				state.result.fps.repeatability.minimum &&
				state.result.fps.repeatability.maximum) {
				ImGui::TextDisabled(
					T(
						TKEY("result.repeatability_band_fps"),
						"Observed blocks: median %+.1f FPS | range %+.1f to %+.1f"),
					*state.result.fps.repeatability.median,
					*state.result.fps.repeatability.minimum,
					*state.result.fps.repeatability.maximum);
			}
			if (state.result.fps.currentBefore &&
				state.result.fps.comparison) {
				ImGui::TextDisabled(
					T(
						TKEY("result.window_means_fps"),
						"Captured throughput: %.1f FPS current | %.1f FPS comparison"),
					*state.result.fps.currentBefore,
					*state.result.fps.comparison);
			}
		}

		ImGui::Spacing();
		RenderCostWindowDiagnostics(
			T(TKEY("status.current_before"), "current"),
			state.result.currentBeforeDiagnostics);
		RenderCostWindowDiagnostics(
			T(TKEY("status.comparison"), "comparison"),
			state.result.comparisonDiagnostics);
		if (HasUnreliableFeatureCostValue(state.result)) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				T(
					TKEY("result.repeatability_warning"),
					"The effect was below the practical floor or block directions were mixed. The averages remain valid for the captured scene, but no reliable direction is claimed."));
		}
		if (state.frameGenerationTemporarilyDisabled) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				T(
					TKEY("result.frame_generation_excluded"),
					"Frame generation was temporarily disabled and restored. These values measure game-produced frames only and exclude frame-generation cost and output FPS."));
		}
		if (state.instrumentationLimited) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				T(
					TKEY("result.instrumentation_limited"),
					"Some GPU or CPU profiler samples could not be matched. Total frame time remains available; affected metrics are unavailable or marked as having insufficient block coverage."));
		}

		if (state.result.presentSynced) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				T(
					TKEY("result.vsync_warning"),
					"Game FPS is VSync-synced. Total/FPS can hide workload changes below the sync budget; use whole-frame GPU and CPU deltas as supporting metrics."));
		} else if (state.result.framePaced) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				T(
					TKEY("result.pacing_warning"),
					"Game FPS appears externally paced or limited. Total/FPS can hide workload changes below that pacing budget; use whole-frame GPU and CPU deltas as supporting metrics."));
		}
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
				(!feature->HasPerformanceSettings() && !feature->SupportsPerformanceCostMeasurement())) {
				continue;
			}
			features.push_back(feature);
		}

		std::ranges::sort(features, [](Feature* lhs, Feature* rhs) {
			const int lhsOrder =
				lhs->GetPerformanceTuningConfig().order < 0 ? 1000 : lhs->GetPerformanceTuningConfig().order;
			const int rhsOrder =
				rhs->GetPerformanceTuningConfig().order < 0 ? 1000 : rhs->GetPerformanceTuningConfig().order;
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
		Feature* feature,
		FeatureCostMeasurementState& state,
		const json& currentUiState)
	{
		if (state.stage != FeatureCostMeasurementStage::Complete)
			return;

		if (!state.hasResultScene ||
			HasMaterialSceneChange(
				state.resultScene,
				CaptureSceneFingerprint()) ||
			!PerformanceTuning::AreJsonValuesEquivalent(
				currentUiState,
				state.resultSettingsState) ||
			!feature->IsPerformanceTuningApplicable() ||
			IsFeatureControlledBySceneSettings(feature)) {
			state = {};
		}
	}
	void SyncFeatureCostVanityCameraSuppression()
	{
		if (!g_quickScan.controller.Active() && !g_featureCostBatch.active && !g_monoSweep.Active() && !IsAnyFeatureCostMeasurementLocked())
			g_vanityCameraSuppression.Release();
	}

	double GetFeatureCostRestartCooldownRemaining(double currentTime)
	{
		return std::max(0.0, g_restartAllowedTime - currentTime);
	}

	RE::FormID GetMeasurementCellId()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		return cell ? cell->GetFormID() : 0;
	}

	const char* GetFeatureCostEnvironmentError()
	{
		if (Util::IsRuntimeToggleBlocked(globals::state) || GetMeasurementCellId() == 0)
			return "not_in_game";
		if (!globals::menu)
			return "menu_unavailable";
		if (auto* editor = EditorWindow::GetSingleton(); editor && editor->open)
			return "editor_open";
		return nullptr;
	}

	const char* GetFeatureCostStartError(double currentTime)
	{
		if (PerformanceTuningRenderer::HasActiveMeasurements())
			return "measurement_busy";
		if (!globals::profiler || !globals::profiler->IsInitialized())
			return "profiler_unavailable";
		if (globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running)
			return "profiler_busy";
		if (globals::profiler->GetCaptureModeLimit() == Profiler::CaptureMode::None)
			return "profiler_capture_blocked";
		if (!IsFeatureCostEnvironmentEligible())
			return "scene_not_ready";
		if (const char* error = GetFeatureCostEnvironmentError())
			return error;
		if (GetFeatureCostRestartCooldownRemaining(currentTime) > 0.0)
			return "restart_cooldown";
		return nullptr;
	}

	const char* GetFeatureToggleBlockReason(Feature* feature)
	{
		if (const char* reason = feature->GetPerformanceToggleBlockReason())
			return reason;
		auto* sceneSettings = SceneSettingsManager::GetSingleton();
		const auto name = feature->GetShortName();
		if (sceneSettings->HasActiveSettingsForFeature(name) && !sceneSettings->IsFeaturePaused(name))
			return T(TKEY("toggle.scene_owned"), "Pause this feature's scene-specific settings before toggling or measuring it.");
		return nullptr;
	}

	PerformanceQuickScan::RenderConfiguration CaptureQuickScanRenderConfiguration()
	{
		return {
			.shadersEnabled = globals::shaderCache->IsEnabled(),
			.shadersRequested = globals::shaderCache->IsEnableRequested(),
			.definesGeneration = globals::state->GetShaderDefinesGeneration(),
			.blockedShaderIndex = globals::shaderCache->blockedKeyIndex,
		};
	}

	json CaptureQuickScanSettings()
	{
		json settings = json::object();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->loaded)
				feature->SaveSettings(settings[feature->GetShortName()]);
		}
		return settings;
	}

	const char* QuickScanPhaseName()
	{
		using enum PerformanceQuickScan::Phase;
		switch (g_quickScan.controller.phase) {
		case AwaitingMenuClose:
			return "awaiting_menu_close";
		case Settling:
			return "settling";
		case Capturing:
			return "capturing";
		case Complete:
			return "complete";
		case Failed:
			return "failed";
		case Cancelled:
			return "cancelled";
		default:
			return "idle";
		}
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	json QuickScanStatus()
	{
		json rows = json::array();
		for (const auto& row : g_quickScan.rows) {
			rows.push_back({ { "feature", row.feature }, { "label", row.label }, { "coverage", row.coverage },
				{ "gpuMs", row.gpuMs ? json(*row.gpuMs) : json(nullptr) },
				{ "cpuMs", row.cpuMs ? json(*row.cpuMs) : json(nullptr) } });
		}
		return { { "phase", QuickScanPhaseName() }, { "active", g_quickScan.controller.Active() },
			{ "captureId", g_quickScan.captureId }, { "requestedFrames", PerformanceQuickScan::kCaptureFrames },
			{ "renderConfiguration", { { "shadersEnabled", g_quickScan.controller.renderConfiguration.shadersEnabled },
										 { "shaderEnableRequested", g_quickScan.controller.renderConfiguration.shadersRequested },
										 { "shaderDefinesGeneration", g_quickScan.controller.renderConfiguration.definesGeneration },
										 { "blockedShaderIndex", g_quickScan.controller.renderConfiguration.blockedShaderIndex } } },
			{ "resolvedFrames", g_quickScan.resolvedFrames }, { "settleSeconds", PerformanceQuickScan::kSettleSeconds },
			{ "invalidGpuFrames", g_quickScan.invalidGpuFrames }, { "invalidCpuFrames", g_quickScan.invalidCpuFrames },
			{ "maximumSeconds", PerformanceQuickScan::kMaximumSeconds },
			{ "excludedTimings", json::array({ "shared_shader_work", "other_gpu_queues", "uninstrumented_work" }) },
			{ "captureSeconds", g_quickScan.captureSeconds }, { "timingSemantics", "instrumented_self_time_ms_per_frame" },
			{ "isOnOffCost", false }, { "failure", g_quickScan.failure.empty() ? json(nullptr) : json(g_quickScan.failure) },
			{ "results", std::move(rows) } };
	}

#endif

	void FinishQuickScan(PerformanceQuickScan::Phase phase, std::string failure, bool reopen)
	{
		auto& scan = g_quickScan;
		scan.capture.Release();
		scan.controller.phase = phase;
		scan.failure = std::move(failure);
		if (phase != PerformanceQuickScan::Phase::Complete)
			scan.rows.clear();
		SyncFeatureCostVanityCameraSuppression();
		if (reopen && scan.reopenMenu && globals::menu && !globals::menu->IsEnabled)
			globals::menu->OpenMenu();
	}

	const char* StartQuickScan(bool devBenchOwned)
	try {
		if (const auto* error = GetFeatureCostStartError(ImGui::GetTime()))
			return error;
		if (!globals::profiler || !globals::profiler->IsInitialized())
			return "profiler_unavailable";
		if (!globals::shaderCache)
			return "renderer_unavailable";
		if (globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running)
			return "profiler_busy";
		if (ABTestingManager::GetSingleton()->IsEnabled())
			return "ab_testing_active";
		QuickScanState pending;
		pending.devBenchOwned = devBenchOwned;
		pending.cell = GetMeasurementCellId();
		pending.settings = CaptureQuickScanSettings();
		pending.reopenMenu = globals::menu->IsEnabled;
		if (!g_vanityCameraSuppression.Acquire())
			return "camera_suppression_failed";
		RestoreProfilerStateAfterPerformanceTuning();
		pending.controller.Begin(ImGui::GetTime(), pending.reopenMenu, CaptureQuickScanRenderConfiguration());
		g_quickScan = std::move(pending);
		if (g_quickScan.reopenMenu)
			globals::menu->CloseMenu();
		return nullptr;
	} catch (const std::exception& error) {
		logger::error("Quick scan could not start: {}", error.what());
		if (g_quickScan.controller.Active())
			FinishQuickScan(PerformanceQuickScan::Phase::Failed, T("menu.performance_tuning.shared.quick_scan_initialization_failed", "Quick scan initialization failed."));
		else
			SyncFeatureCostVanityCameraSuppression();
		return "quick_scan_start_failed";
	}

	bool QuickScanReady()
	{
		auto& upscaling = globals::features::upscaling;
		const auto profile = upscaling.GetMonoConfiguredProfile();
		if ((globals::game::ui && globals::game::ui->GameIsPaused()) ||
			(globals::shaderCache && globals::shaderCache->IsCompiling()) ||
			!IsFeatureCostEnvironmentEligible() || upscaling.IsTextureStreamingTransitionActive() ||
			!upscaling.IsMonoProfileSelected(profile.method, profile.qualityMode, profile.dlssPreset, profile.fsrAutomatic))
			return false;
		for (auto* feature : Feature::GetFeatureList()) {
			// Named-pass capture observes the user's live presentation configuration.
			// The cost-test readiness contract additionally disables frame generation.
			if (feature != &upscaling && feature->loaded && Util::FeatureProfiling::Find(feature->GetShortName()) &&
				!PerformanceQuickScan::Ready(*feature))
				return false;
		}
		return true;
	}

	void CollectQuickScanResults(const std::vector<Profiler::TimerResult>& timers)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			const auto name = feature->GetShortName();
			const auto* view = Util::FeatureProfiling::Find(name);
			if (!feature->loaded || !view)
				continue;
			const auto samples = std::span<const Profiler::TimerResult>(timers);
			g_quickScan.rows.push_back({ name, feature->GetDisplayName(), view->OwnedCoverage(),
				PerformanceQuickScan::Mean(*view, samples, false, PerformanceQuickScan::kCaptureFrames),
				PerformanceQuickScan::Mean(*view, samples, true, PerformanceQuickScan::kCaptureFrames) });
		}
		std::stable_sort(g_quickScan.rows.begin(), g_quickScan.rows.end(), [](const auto& left, const auto& right) {
			return left.gpuMs.value_or(-1.0) > right.gpuMs.value_or(-1.0);
		});
	}

	void UpdateQuickScan(double now)
	try {
		using enum PerformanceQuickScan::Phase;
		using enum PerformanceQuickScan::Action;
		auto& scan = g_quickScan;
		if (!scan.controller.Active())
			return;
		if (GetFeatureCostEnvironmentError() || GetMeasurementCellId() != scan.cell ||
			ABTestingManager::GetSingleton()->IsEnabled() ||
			(scan.controller.phase != AwaitingMenuClose && globals::menu->IsEnabled)) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.the_scene_or_measurement_ownership_changed", "The scene or measurement ownership changed."));
			return;
		}
		if (!globals::shaderCache) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.shader_settings_changed_during_the_quick_scan", "Shader settings changed during the quick scan."));
			return;
		}
		if (!globals::profiler || !globals::profiler->IsInitialized()) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.the_profiler_became_unavailable", "The profiler became unavailable."));
			return;
		}
		auto* profiler = globals::profiler;
		const auto action = scan.controller.Poll(now, QuickScanReady(), CaptureQuickScanRenderConfiguration());
		if (action == ConfigurationChanged) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.shader_settings_changed_during_the_quick_scan", "Shader settings changed during the quick scan."));
			return;
		}
		if (action == TimedOut || action == Interrupted) {
			FinishQuickScan(Failed, action == TimedOut ? T(TKEY("quick_scan.timeout"), "The quick scan timed out.") : T(TKEY("quick_scan.unready"), "Rendering became unready during capture."));
			return;
		}
		if (action == StartCapture) {
			if (profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running) {
				FinishQuickScan(Failed, T("menu.performance_tuning.shared.another_profiler_capture_is_active", "Another profiler capture is active."));
				return;
			}
			if (CaptureQuickScanSettings() != scan.settings) {
				FinishQuickScan(Failed, T("menu.performance_tuning.shared.feature_settings_changed_while_settling", "Feature settings changed while settling."));
				return;
			}
			if (!scan.capture.Start(*profiler, PerformanceQuickScan::kCaptureFrames)) {
				FinishQuickScan(Failed, T("menu.performance_tuning.shared.the_profiler_could_not_start_the_capture", "The profiler could not start the capture."));
				return;
			}
			scan.captureId = scan.capture.SessionId();
			scan.gpuRefusals = profiler->GetSlotRefusals();
			scan.cpuRefusals = profiler->GetCpuSlotRefusals();
			scan.captureStarted = now;
			scan.controller.phase = Capturing;
		}
		if (scan.controller.phase != Capturing)
			return;
		const auto progress = profiler->GetBoundedCaptureProgress();
		scan.captureSeconds = now - scan.captureStarted;
		if (progress.sessionId != scan.captureId || progress.state == Profiler::CaptureSessionState::Cancelled ||
			!profiler->IsUserEnabled()) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.the_owned_profiler_capture_was_interrupted", "The owned profiler capture was interrupted."));
			return;
		}
		scan.resolvedFrames = progress.resolvedFrames;
		scan.invalidGpuFrames = progress.invalidGpuFrames;
		scan.invalidCpuFrames = progress.invalidCpuFrames;
		if (scan.invalidGpuFrames || scan.invalidCpuFrames) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.profiler_queries_were_missing_or_invalid_results_would_be", "Profiler queries were missing or invalid; results would be incomplete."));
			return;
		}
		if (profiler->GetSlotRefusals() != scan.gpuRefusals || profiler->GetCpuSlotRefusals() != scan.cpuRefusals) {
			FinishQuickScan(Failed, T("menu.performance_tuning.shared.profiler_capacity_was_exceeded_results_would_be_incomplete", "Profiler capacity was exceeded; results would be incomplete."));
			return;
		}
		if (progress.state == Profiler::CaptureSessionState::Completed) {
			const auto* timers = profiler->GetBoundedCaptureResults(scan.captureId);
			if (!timers || progress.resolvedFrames != PerformanceQuickScan::kCaptureFrames ||
				CaptureQuickScanSettings() != scan.settings) {
				FinishQuickScan(Failed, T("menu.performance_tuning.shared.capture_data_or_feature_settings_changed", "Capture data or feature settings changed."));
				return;
			}
			CollectQuickScanResults(*timers);
			FinishQuickScan(Complete);
		}
	} catch (const std::exception& error) {
		logger::error("Quick scan failed: {}", error.what());
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, T("menu.performance_tuning.shared.quick_scan_data_collection_failed", "Quick scan data collection failed."));
	}


	void InvalidateFeatureCostResults()
	{
		if (!g_quickScan.controller.Active())
			g_quickScan = {};
		std::erase_if(g_costMeasurementStates, [](const auto& entry) { return !IsFeatureCostMeasurementLocked(entry.second); });
		if (!g_featureCostBatch.active)
			g_featureCostBatch = {};
	}

	bool SetRuntimeFeatureEnabled(Feature* feature, bool enabled)
	{
		if (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement() ||
			GetFeatureToggleBlockReason(feature) || PerformanceTuningRenderer::HasActiveMeasurements())
			return false;
		if (feature->IsPerformanceToggleEnabled() == enabled)
			return true;
		const auto shortName = feature->GetShortName();
		auto& saved = g_disabledFeatureConfigurations[shortName];
		if (!PerformanceTuningController::SetEnabled(*feature, enabled, saved))
			return false;
		if (!saved)
			g_disabledFeatureConfigurations.erase(shortName);
		InvalidateFeatureCostResults();
		return true;
	}

	bool StartNextFeatureCostBatchMeasurement(double currentTime)
	{
		auto& batch = g_featureCostBatch;
		if (GetFeatureCostEnvironmentError() || GetMeasurementCellId() != batch.cellFormId || batch.nextFeatureIndex >= batch.features.size())
			return false;
		auto* feature = FindFeatureByShortName(batch.features[batch.nextFeatureIndex]);
		if (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement() ||
			!feature->IsPerformanceCostMeasurementEnabled() || GetFeatureToggleBlockReason(feature))
			return false;
		auto& state = g_costMeasurementStates[feature->GetShortName()];
		CaptureProfilerStateForPerformanceTuning();
		const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary({}, Profiler::CaptureMode::WholeFrameOnly);
		StartFeatureCostMeasurement(feature, state, timing, currentTime, true, batch.devBenchOwned);
		if (!IsFeatureCostMeasurementRunning(state))
			return false;
		++batch.nextFeatureIndex;
		return true;
	}

	const char* StartFeatureCostBatch(bool devBenchOwned)
	{
		const double currentTime = ImGui::GetTime();
		if (const char* error = GetFeatureCostStartError(currentTime))
			return error;

		std::vector<std::string> enabledFeatures;
		for (auto* feature : BuildPerformanceFeatureList()) {
			if (feature->IsPerformanceCostMeasurementEnabled() && !GetFeatureToggleBlockReason(feature))
				enabledFeatures.push_back(feature->GetShortName());
		}
		if (enabledFeatures.empty())
			return "no_enabled_features";

		InvalidateFeatureCostResults();
		g_featureCostBatch.active = true;
		g_featureCostBatch.devBenchOwned = devBenchOwned;
		g_featureCostBatch.reopenMenuOnCompletion = globals::menu->IsEnabled;
		g_featureCostBatch.features = std::move(enabledFeatures);
		g_featureCostBatch.cellFormId = GetMeasurementCellId();
		if (!StartNextFeatureCostBatchMeasurement(currentTime)) {
			g_featureCostBatch.active = false;
			RestoreProfilerStateAfterPerformanceTuning();
			g_featureCostBatch.failureMessage = T("menu.performance_tuning.shared.the_feature_comparison_could_not_start", "The feature comparison could not start.");
			SyncFeatureCostVanityCameraSuppression();
			return "measurement_start_failed";
		}
		return nullptr;
	}

	bool UpdateFeatureCostBatch(double currentTime)
	{
		auto& batch = g_featureCostBatch;
		if (!batch.active)
			return false;
		if (!IsAnyFeatureCostMeasurementLocked() &&
			(GetFeatureCostEnvironmentError() || GetMeasurementCellId() != batch.cellFormId)) {
			batch.failureMessage = T("menu.performance_tuning.shared.feature_comparisons_stopped_because_the_gameplay_scene_changed_completed", "Feature comparisons stopped because the gameplay scene changed. Completed results are retained.");
			batch.active = false;
			return batch.reopenMenuOnCompletion;
		}

		const auto previous = batch.nextFeatureIndex > 0 && batch.nextFeatureIndex <= batch.features.size() ?
		                          g_costMeasurementStates.find(batch.features[batch.nextFeatureIndex - 1]) :
		                          g_costMeasurementStates.end();
		const bool previousComplete = previous != g_costMeasurementStates.end() && previous->second.stage == FeatureCostMeasurementStage::Complete;
		const bool previousFailed = previous != g_costMeasurementStates.end() && !previous->second.failureReason.empty();
		switch (PerformanceTuningController::NextBatchAction(batch.nextFeatureIndex, batch.features.size(),
			IsAnyFeatureCostMeasurementLocked(), previousComplete, previousFailed, GetFeatureCostRestartCooldownRemaining(currentTime))) {
		case PerformanceTuningController::BatchAction::Wait:
			break;
		case PerformanceTuningController::BatchAction::Finish:
			batch.active = false;
			break;
		case PerformanceTuningController::BatchAction::StartNext:
			if (StartNextFeatureCostBatchMeasurement(currentTime))
				break;
			[[fallthrough]];
		case PerformanceTuningController::BatchAction::Fail:
			batch.failureMessage = T("menu.performance_tuning.shared.feature_comparisons_stopped_because_a_measurement_could_not_complete", "Feature comparisons stopped because a measurement could not complete. Completed results are retained.");
			batch.active = false;
			break;
		}
		return !batch.active && batch.reopenMenuOnCompletion;
	}

	void RenderQuickScan()
	{
		MenuUI::SectionHeading(T("menu.performance_tuning.shared.quick_scan", "Quick scan"));
		MenuUI::DetailText(T("menu.performance_tuning.shared.keep_the_scene_still_the_menu_closes_csx_waits", "Keep the scene still. The menu closes; CSX waits five seconds, then captures 300 frames for all instrumented features."));
		MenuUI::DetailText(T("menu.performance_tuning.shared.instrumented_cpu_gpu_self_times_not_on_off_savings", "Instrumented CPU/GPU self times, not on/off savings. Existing on/off comparisons are unchanged."));
		{
			auto disabled = Util::DisableGuard(PerformanceTuningRenderer::HasActiveMeasurements());
			if (ImGui::Button(T("menu.performance_tuning.shared.start_quick_scan", "Start quick scan"))) {
				const char* error = StartQuickScan(false);
				g_featureCostUiMessage = error ? fmt::format(fmt::runtime(T(TKEY("quick_scan.start_failed"), "Quick scan could not start: {}")), error) : "";
			}
		}
		if (!g_featureCostUiMessage.empty())
			MenuUI::DetailText(g_featureCostUiMessage.c_str());
		if (!g_quickScan.failure.empty())
			MenuUI::DetailText(g_quickScan.failure.c_str());
		if (g_quickScan.controller.phase != PerformanceQuickScan::Phase::Complete)
			return;
		ImGui::Text(T("menu.performance_tuning.shared.captured_u_frames_in_1f_seconds_after_settling", "Captured %u frames in %.1f seconds after settling."), g_quickScan.resolvedFrames, g_quickScan.captureSeconds);
		MenuUI::DetailText(T("menu.performance_tuning.shared.sorted_by_gpu_time_shared_only_inactive_or_incomplete", "Sorted by GPU time. -- = shared-only, inactive or incomplete coverage. Excludes shared shader work and other GPU queues."));
		if (ImGui::BeginTable("##QuickScanResults", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
			const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn(T("menu.performance_tuning.shared.feature", "Feature"));
			ImGui::TableSetupColumn(T("menu.performance_tuning.shared.gpu_ms", "GPU (ms)"));
			ImGui::TableSetupColumn(T("menu.performance_tuning.shared.cpu_ms", "CPU (ms)"));
			ImGui::TableSetupColumn(T("menu.performance_tuning.shared.coverage", "Coverage"));
			ImGui::TableHeadersRow();
			for (const auto& row : g_quickScan.rows) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(row.label.c_str());
				for (const auto value : { row.gpuMs, row.cpuMs }) {
					ImGui::TableNextColumn();
					if (value)
						ImGui::Text("%.3f", *value);
					else
						ImGui::TextDisabled("--");
				}
				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", row.coverage.c_str());
			}
		}
	}

	void RenderFeatureSetMeasurement(const std::vector<Feature*>& features)
	{
		MenuUI::SectionHeading(T(TKEY("batch.heading"), "Compare enabled features"));
		MenuUI::DetailText(T(TKEY("batch.description"), "Measure enabled features one at a time against Off or None. Each run uses the SE whole-frame comparison and restores its settings before the next feature."));
		{
			auto disabled = Util::DisableGuard(PerformanceTuningRenderer::HasActiveMeasurements());
			if (ImGui::Button(T(TKEY("batch.start"), "Measure all enabled features"))) {
				const auto* error = StartFeatureCostBatch(false);
				g_featureCostUiMessage = error ? fmt::format(fmt::runtime(T(TKEY("batch.start_failed"), "Feature comparisons could not start: {}")), error) : "";
			}
		}
		if (!g_featureCostBatch.failureMessage.empty())
			MenuUI::DetailText(g_featureCostBatch.failureMessage.c_str());
		if (!g_featureCostUiMessage.empty())
			MenuUI::DetailText(g_featureCostUiMessage.c_str());
		if (ImGui::BeginTable("##FeatureBatchResults", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
			const SKSE::stl::scope_exit end([] { ImGui::EndTable(); });
			for (const auto* label : { T(TKEY("batch.feature"), "Feature"), T(TKEY("batch.enabled"), "Enabled"), T(TKEY("batch.frame"), "Frame (ms)"), T(TKEY("batch.gpu"), "GPU (ms)"), T(TKEY("batch.cpu"), "CPU (ms)"), T(TKEY("batch.fps_loss"), "FPS loss (%)") })
				ImGui::TableSetupColumn(label);
			ImGui::TableHeadersRow();
			for (auto* feature : features) {
				if (!feature || !feature->SupportsPerformanceCostMeasurement())
					continue;
				const auto name = feature->GetShortName();
				ImGui::PushID(name.c_str());
				const SKSE::stl::scope_exit pop([] { ImGui::PopID(); });
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(feature->GetDisplayName().c_str());
				ImGui::TableNextColumn();
				PerformanceTuningRenderer::RenderFeatureEnabledControl(feature);
				const auto found = g_costMeasurementStates.find(name);
				const auto* result = found != g_costMeasurementStates.end() && found->second.stage == FeatureCostMeasurementStage::Complete ? &found->second.result : nullptr;
				for (const auto* metric : { result ? &result->present : nullptr, result ? &result->wholeFrameGpu : nullptr, result ? &result->wholeFrameCpu : nullptr }) {
					ImGui::TableNextColumn();
					if (metric && metric->valueMs) {
						double percentage = 0.0;
						if (metric->currentBeforeMeanMs && PerformanceTuningStatistics::TryGetCostPercentage(*metric->valueMs, *metric->currentBeforeMeanMs, percentage))
							ImGui::Text("%+.3f (%+.1f%%)", *metric->valueMs, percentage);
						else
							ImGui::Text("%+.3f", *metric->valueMs);
					} else
						ImGui::TextDisabled("--");
				}
				ImGui::TableNextColumn();
				double fpsLoss = 0.0;
				if (result && result->fps.value && result->fps.comparison &&
					PerformanceTuningStatistics::TryGetCostPercentage(-*result->fps.value, *result->fps.comparison, fpsLoss))
					ImGui::Text("%+.1f%%", fpsLoss);
				else
					ImGui::TextDisabled("--");
			}
		}
		MenuUI::DetailText(T(TKEY("batch.interpretation"), "Deltas are current settings minus Off or None. Use the feature results to inspect coverage, repeatability and frame-pacing warnings."));
	}


	bool IsMonoProfileSelected(const Upscaling::MonoProfile& profile)
	{
		return globals::features::upscaling.IsMonoProfileSelected(profile.method, profile.qualityMode, profile.dlssPreset, profile.fsrAutomatic);
	}

	bool StageMonoSweepCase()
	{
		auto& sweep = g_monoSweep;
		if (sweep.index >= sweep.cases.size())
			return false;
		const auto& profile = sweep.cases[sweep.index].profile;
		if (!globals::features::upscaling.RequestMonoProfile(profile.method, profile.qualityMode, profile.dlssPreset, profile.fsrAutomatic))
			return false;
		sweep.phase = MonoSweepPhase::Staging;
		sweep.phaseStarted = ImGui::GetTime();
		return true;
	}

	void BeginMonoSweepRestore(MonoSweepPhase terminal, std::string failure)
	{
		auto& sweep = g_monoSweep;
		if (!sweep.Active())
			return;
		sweep.terminalPhase = terminal;
		if (!failure.empty())
			sweep.failure = std::move(failure);
		sweep.phase = MonoSweepPhase::Restoring;
		sweep.phaseStarted = ImGui::GetTime();
		sweep.lastRestoreAttempt = -1.0;
		sweep.restorePending = true;
	}

	bool MonoSweepOriginalSettingsMatch()
	{
		const auto& sweep = g_monoSweep;
		auto& upscaling = globals::features::upscaling;
		const auto frameGeneration = upscaling.CaptureFrameGenerationSettingsForPerformanceMeasurement();
		return PerformanceTuning::AreJsonValuesEquivalent(upscaling.CapturePerformanceCostMeasurementState(), sweep.originalState) &&
			frameGeneration.mode == sweep.originalFrameGeneration.mode &&
			frameGeneration.forceEnable == sweep.originalFrameGeneration.forceEnable &&
			frameGeneration.allowInMenus == sweep.originalFrameGeneration.allowInMenus;
	}

	bool RestoreMonoSweepOriginalSettings()
	{
		if (MonoSweepOriginalSettingsMatch())
			return true;
		auto& upscaling = globals::features::upscaling;
		upscaling.RestorePerformanceCostMeasurementState(g_monoSweep.originalState);
		upscaling.RestoreFrameGenerationSettingsAfterPerformanceMeasurement(g_monoSweep.originalFrameGeneration);
		return MonoSweepOriginalSettingsMatch();
	}

	// A new configuration may take ownership as soon as the saved settings are restored.
	// It must not later be overwritten by the sweep waiting for a rendered frame to apply them.
	bool RelinquishMonoSweepForConfiguration()
	{
		if (!g_monoSweep.Active())
			return true;
		if (g_monoSweep.phase != MonoSweepPhase::Restoring)
			return false;
		try {
			if (!RestoreMonoSweepOriginalSettings())
				return false;
			g_monoSweep.restorePending = false;
			g_monoSweep.phase = g_monoSweep.terminalPhase;
			g_closedMenuMeasurement = IsAnyFeatureCostMeasurementLocked();
			SyncProfilerCaptureModeLimit();
			SyncFeatureCostVanityCameraSuppression();
			RestoreProfilerStateAfterPerformanceTuning();
			return true;
		} catch (const std::exception& error) {
			logger::error("Could not restore upscaling before a configuration change: {}", error.what());
			return false;
		}
	}

	void UpdateMonoSweep(double now)
	{
		auto& sweep = g_monoSweep;
		if (!sweep.Active())
			return;
		auto& upscaling = globals::features::upscaling;
		try {
			if (sweep.phase == MonoSweepPhase::Restoring) {
				auto found = g_costMeasurementStates.find("Upscaling");
				if (found != g_costMeasurementStates.end() && IsFeatureCostMeasurementLocked(found->second)) {
					if (IsFeatureCostMeasurementRunning(found->second))
						FailFeatureCostMeasurement(&upscaling, found->second, T(TKEY("sweep.stopped"), "Upscaling sweep stopped."));
					if (found->second.restorePending && !TryRestoreFeatureCostOriginalState(&upscaling, found->second))
						return;
				}
				bool matches = MonoSweepOriginalSettingsMatch();
				if (!matches && (sweep.lastRestoreAttempt < 0.0 || now - sweep.lastRestoreAttempt >= 1.0)) {
					sweep.lastRestoreAttempt = now;
					matches = RestoreMonoSweepOriginalSettings();
				}
				if (matches && IsMonoProfileSelected(sweep.originalProfile)) {
					sweep.restorePending = false;
					sweep.phase = sweep.terminalPhase;
					g_closedMenuMeasurement = false;
					SyncProfilerCaptureModeLimit();
					SyncFeatureCostVanityCameraSuppression();
					RestoreProfilerStateAfterPerformanceTuning();
					if (sweep.reopenMenu && globals::menu)
						globals::menu->OpenMenu();
				} else if (now - sweep.phaseStarted > kFeatureCostTransitionTimeoutSeconds && sweep.failure.empty()) {
					sweep.failure = T(TKEY("sweep.restore_wait"), "Waiting for the original upscaling state to finish restoring.");
				}
				return;
			}
			if (GetFeatureCostEnvironmentError() || !IsFeatureCostEnvironmentEligible() ||
				HasMaterialSceneChange(sweep.scene, CaptureSceneFingerprint()) || (globals::menu && globals::menu->IsEnabled)) {
				BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.scene_changed"), "Upscaling sweep stopped because the scene or measurement ownership changed."));
				return;
			}
			if (sweep.phase == MonoSweepPhase::Cooldown) {
				if (now < g_restartAllowedTime)
					return;
				if (!StageMonoSweepCase())
					BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.profile_rejected"), "The next upscaling profile could not be staged."));
				return;
			}
			if (sweep.phase == MonoSweepPhase::Staging) {
				const auto& profile = sweep.cases[sweep.index].profile;
				if (upscaling.GetMonoConfiguredProfile() != profile) {
					BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.profile_changed"), "The selected upscaling profile changed during the sweep."));
					return;
				}
				if (!IsMonoProfileSelected(profile) || !upscaling.IsPerformanceCostMeasurementReady()) {
					if (now - sweep.phaseStarted > kFeatureCostTransitionTimeoutSeconds)
						BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.not_ready"), "The selected upscaling profile did not become ready."));
					return;
				}
				if (profile.fsrAutomatic && !upscaling.GetMonoEffectiveProfile().fsrAutomatic) {
					BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.provider_fallback"), "The automatic FSR runtime fell back; this case cannot be reported as the requested runtime."));
					return;
				}
				const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary({}, Profiler::CaptureMode::WholeFrameOnly);
				auto& state = g_costMeasurementStates["Upscaling"];
				StartFeatureCostMeasurement(&upscaling, state, timing, now, true, sweep.devBenchOwned);
				if (!IsFeatureCostMeasurementRunning(state)) {
					BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.case_start_failed"), "The upscaling comparison could not start."));
					return;
				}
				sweep.phase = MonoSweepPhase::Measuring;
				return;
			}
			if (sweep.phase == MonoSweepPhase::Measuring) {
				const auto found = g_costMeasurementStates.find("Upscaling");
				if (found == g_costMeasurementStates.end()) {
					BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.case_missing"), "The owned upscaling comparison was interrupted."));
					return;
				}
				if (IsFeatureCostMeasurementLocked(found->second))
					return;
				if (found->second.stage != FeatureCostMeasurementStage::Complete) {
					BeginMonoSweepRestore(MonoSweepPhase::Failed, found->second.failureReason);
					return;
				}
				sweep.cases[sweep.index].result = found->second.result;
				sweep.cases[sweep.index].measured = true;
				++sweep.index;
				if (sweep.index == sweep.cases.size())
					BeginMonoSweepRestore(MonoSweepPhase::Complete);
				else {
					sweep.phase = MonoSweepPhase::Cooldown;
					sweep.phaseStarted = now;
				}
			}
		} catch (const std::exception& error) {
			logger::error("Upscaling cost sweep failed: {}", error.what());
			BeginMonoSweepRestore(MonoSweepPhase::Failed, T(TKEY("sweep.exception"), "Upscaling comparison failed; restoring the original state."));
		}
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	const char* FeatureCostStageName(FeatureCostMeasurementStage stage)
	{
		switch (stage) {
		case FeatureCostMeasurementStage::AwaitingMenuClose: return "awaiting_menu_close";
		case FeatureCostMeasurementStage::Settling: return "settling";
		case FeatureCostMeasurementStage::Measuring: return "measuring";
		case FeatureCostMeasurementStage::Draining: return "draining";
		case FeatureCostMeasurementStage::Restoring: return "restoring";
		case FeatureCostMeasurementStage::Complete: return "complete";
		case FeatureCostMeasurementStage::Failed: return "failed";
		default: return "idle";
		}
	}

	const char* MonoSweepPhaseName()
	{
		switch (g_monoSweep.phase) {
		case MonoSweepPhase::Staging: return "staging";
		case MonoSweepPhase::Measuring: return "measuring";
		case MonoSweepPhase::Cooldown: return "cooldown";
		case MonoSweepPhase::Restoring: return "restoring_original";
		case MonoSweepPhase::Complete: return "complete";
		case MonoSweepPhase::Failed: return "failed";
		case MonoSweepPhase::Cancelled: return "cancelled";
		default: return "idle";
		}
	}

	json OptionalNumber(const std::optional<double>& value) { return value ? json(*value) : json(nullptr); }

	json MetricDeltaJson(const PerformanceTuning::MetricDelta& metric)
	{
		double percent = 0.0;
		const bool hasPercent = metric.valueMs && metric.currentBeforeMeanMs &&
			PerformanceTuningStatistics::TryGetCostPercentage(*metric.valueMs, *metric.currentBeforeMeanMs, percent);
		return { { "available", metric.IsAvailable() }, { "reliable", metric.IsReliable() },
			{ "reliability", static_cast<uint32_t>(metric.reliability) }, { "direction", metric.direction },
			{ "valueMs", OptionalNumber(metric.valueMs) }, { "currentMeanMs", OptionalNumber(metric.currentBeforeMeanMs) },
			{ "comparisonMeanMs", OptionalNumber(metric.comparisonMeanMs) }, { "costPercent", hasPercent ? json(percent) : json(nullptr) },
			{ "blockMinimumMs", OptionalNumber(metric.repeatability.minimum) }, { "blockMaximumMs", OptionalNumber(metric.repeatability.maximum) },
			{ "availableBlocks", metric.repeatability.availableBlockCount }, { "agreeingBlocks", metric.repeatability.agreeingBlockCount } };
	}

	json CostResultJson(const PerformanceTuning::CostResult& result)
	{
		return { { "frame", MetricDeltaJson(result.present) }, { "gpu", MetricDeltaJson(result.wholeFrameGpu) },
			{ "cpu", MetricDeltaJson(result.wholeFrameCpu) }, { "fps", { { "value", OptionalNumber(result.fps.value) },
				{ "current", OptionalNumber(result.fps.currentBefore) }, { "comparison", OptionalNumber(result.fps.comparison) } } },
			{ "presentSynced", result.presentSynced }, { "framePaced", result.framePaced } };
	}

	json BlockSignificanceJson(const FeatureCostMeasurementState& state, PerformanceTuning::MetricKind kind)
	{
		using namespace PerformanceTuningStatistics;
		std::array<Moments, PerformanceTuning::kMeasurementBlockCount> current{}, comparison{};
		for (size_t i = 0; i < current.size(); ++i) {
			const auto a = PerformanceTuning::GetBlockMeanMs(state.currentBefore, i, kind);
			const auto b = PerformanceTuning::GetBlockMeanMs(state.comparison, i, kind);
			if (!a || !b)
				return nullptr;
			current[i] = { *a, 1.0 };
			comparison[i] = { *b, 1.0 };
		}
		double aMean = 0.0, aVariance = 0.0, bMean = 0.0, bVariance = 0.0;
		if (!TryGetBlockMeanStatistics(current, aMean, aVariance) || !TryGetBlockMeanStatistics(comparison, bMean, bVariance))
			return nullptr;
		const auto value = EvaluateSignificance(aMean - bMean, std::sqrt(aVariance + bVariance));
		return { { "hasStandardError", value.hasStandardError }, { "standardError", value.standardError },
			{ "pValue", value.pValue }, { "significant", value.significant } };
	}

	void RecordMeasurementTrace(const ProfilingRenderer::PerformanceTimingSummary& timing, double now)
	{
		if (g_traceLast >= 0.0 && now - g_traceLast < 0.1)
			return;
		if (g_traceLast < 0.0)
			g_traceStarted = now;
		g_traceLast = now;
		auto* feature = FindRunningFeature();
		g_measurementTrace.push_back({ g_traceNextSequence++, (now - g_traceStarted) * 1000.0,
			globals::state ? globals::state->frameCount : 0, feature ? feature->GetShortName() : "",
			g_monoSweep.index < g_monoSweep.cases.size() ? g_monoSweep.cases[g_monoSweep.index].id : "", timing });
		while (g_measurementTrace.size() > 8192)
			g_measurementTrace.pop_front();
	}

	json BuildDevBenchMeasurementStatus(uint64_t afterSequence, size_t maximumSamples)
	{
		json availableFeatures = json::array();
		for (auto* feature : BuildPerformanceFeatureList()) {
			if (!feature->SupportsPerformanceCostMeasurement())
				continue;
			const auto config = feature->GetPerformanceTuningConfig();
			const auto* blocked = GetFeatureToggleBlockReason(feature);
			const double comparisonWait = std::max(PerformanceTuning::kComparisonWaitSeconds, feature->GetPerformanceCostMeasurementSettleSeconds(false));
			availableFeatures.push_back({ { "featureShortName", feature->GetShortName() }, { "displayName", feature->GetDisplayName() },
				{ "enabled", feature->IsPerformanceToggleEnabled() }, { "measurementEnabled", feature->IsPerformanceCostMeasurementEnabled() },
				{ "ready", feature->IsPerformanceCostMeasurementReady() }, { "applicable", feature->IsPerformanceTuningApplicable() },
				{ "toggleBlockReason", blocked ? json(blocked) : json(nullptr) },
				{ "comparisonLabel", config.comparisonLabel }, { "comparisonDetails", config.comparisonDetails },
				{ "comparisonWaitMs", comparisonWait * 1000.0 }, { "expectedRunMs", PerformanceTuning::GetExpectedRunSeconds(comparisonWait) * 1000.0 } });
		}
		json measurements = json::array();
		for (const auto& [name, state] : g_costMeasurementStates) {
			measurements.push_back({ { "feature", name }, { "phase", FeatureCostStageName(state.stage) },
				{ "active", IsFeatureCostMeasurementLocked(state) }, { "devBenchOwned", state.devBenchOwned },
				{ "restorePending", state.restorePending }, { "failure", state.failureReason.empty() ? json(nullptr) : json(state.failureReason) },
				{ "result", state.stage == FeatureCostMeasurementStage::Complete ? CostResultJson(state.result) : json(nullptr) },
				{ "blockStatistics", { { "frame", BlockSignificanceJson(state, PerformanceTuning::MetricKind::Present) },
					{ "gpu", BlockSignificanceJson(state, PerformanceTuning::MetricKind::WholeFrameGpu) },
					{ "cpu", BlockSignificanceJson(state, PerformanceTuning::MetricKind::WholeFrameCpu) } } } });
		}
		json cases = json::array();
		for (const auto& item : g_monoSweep.cases) {
			cases.push_back({ { "id", item.id }, { "label", item.label }, { "measured", item.measured },
				{ "method", static_cast<uint32_t>(item.profile.method) }, { "qualityMode", item.profile.qualityMode },
				{ "resolutionScale", Upscaling::GetQualityModeResolutionScale(item.profile.qualityMode) },
				{ "dlssPreset", item.profile.dlssPreset }, { "fsrAutomatic", item.profile.fsrAutomatic },
				{ "result", item.measured ? CostResultJson(item.result) : json(nullptr) } });
		}
		json trace = json::array();
		for (const auto& item : g_measurementTrace) {
			if (item.sequence <= afterSequence)
				continue;
			if (trace.size() >= std::min<size_t>(maximumSamples, 512))
				break;
			const auto& t = item.timing;
			trace.push_back({ { "sequence", item.sequence }, { "elapsedMs", item.elapsedMs }, { "frameCount", item.frame },
				{ "feature", item.feature }, { "caseId", item.caseId }, { "presentIntervalSampleId", t.presentIntervalSampleId },
				{ "wholeFrameSampleId", t.wholeFrameSampleId }, { "wholeFramePresentIntervalSampleId", t.wholeFramePresentIntervalSampleId },
				{ "presentDiscontinuityEpoch", t.presentDiscontinuityEpoch }, { "skippedWholeFrameCaptures", t.skippedWholeFrameCaptureCount },
				{ "frameMs", t.hasPresentIntervalSample ? json(t.presentIntervalSampleMs) : json(nullptr) },
				{ "gpuMs", t.hasWholeFrameGpuSample ? json(t.wholeFrameGpuSampleMs) : json(nullptr) },
				{ "cpuMs", t.hasWholeFrameCpuSample ? json(t.wholeFrameCpuSampleMs) : json(nullptr) } });
		}
		return { { "active", PerformanceTuningRenderer::HasActiveMeasurements() },
			{ "availableFeatureCosts", std::move(availableFeatures) },
			{ "timing", { { "measurementIntervalMs", PerformanceTuning::kMeasurementBlockDurationMs },
				{ "measurementBlockCount", PerformanceTuning::kMeasurementBlockCount }, { "initialWaitMs", PerformanceTuning::kInitialWaitSeconds * 1000.0 },
				{ "comparisonWaitMs", PerformanceTuning::kComparisonWaitSeconds * 1000.0 }, { "restoreWaitMs", PerformanceTuning::kRestoreWaitSeconds * 1000.0 },
				{ "postRunCooldownMs", PerformanceTuning::kRestartCooldownSeconds * 1000.0 } } },
			{ "metricDefinitions", { { "runtime", "se" }, { "version", 1 }, { "frame", "accepted game Present-to-Present interval before frame generation" },
				{ "gpu", "D3D11 whole-frame GPU" }, { "cpu", "QPC render-to-present CPU" },
				{ "comparisonStatistic", "SE duration-weighted block comparison with coverage and repeatability; supplemental block standard error" } } },
			{ "quickScan", QuickScanStatus() }, { "measurements", std::move(measurements) },
			{ "featureBatch", { { "active", g_featureCostBatch.active }, { "features", g_featureCostBatch.features },
				{ "startedCount", g_featureCostBatch.nextFeatureIndex }, { "failure", g_featureCostBatch.failureMessage } } },
			{ "sweepPhase", MonoSweepPhaseName() }, { "currentCaseIndex", g_monoSweep.index }, { "caseCount", g_monoSweep.cases.size() },
			{ "baseline", "none" }, { "matrix", g_monoSweep.matrix }, { "restorePending", g_monoSweep.restorePending },
			{ "failure", g_monoSweep.failure }, { "cases", std::move(cases) },
			{ "trace", std::move(trace) }, { "nextTraceSequence", g_traceNextSequence } };
	}
#endif

}

void PerformanceTuningRenderer::NotifyMenuClosed()
{
	if (g_quickScan.controller.Active()) {
		g_quickScan.controller.MenuClosed(ImGui::GetTime());
		return;
	}
	if (!g_closedMenuMeasurement) {
		CancelActiveMeasurements();
		return;
	}
	const double currentTime = ImGui::GetTime();
	for (auto& [_, state] : g_costMeasurementStates) {
		if (state.stage != FeatureCostMeasurementStage::AwaitingMenuClose)
			continue;
		state.overallStartTime = currentTime;
		BeginFeatureCostSettling(state, FeatureCostMeasurementLeg::CurrentBefore, g_lastTimingSummary, currentTime);
	}
}

void PerformanceTuningRenderer::UpdateClosedMenuMeasurement()
{
	const double now = ImGui::GetTime();
	UpdateQuickScan(now);
	if (now >= g_nextRestoreRetry) {
		g_nextRestoreRetry = now + 1.0;
		for (auto& [name, state] : g_costMeasurementStates)
			if (!IsFeatureCostMeasurementRunning(state) && state.restorePending)
				TryRestoreFeatureCostOriginalState(FindFeatureByShortName(name), state);
	}
	UpdateMonoSweep(now);
	if (!g_closedMenuMeasurement)
		return;

	if (!globals::menu || globals::menu->IsEnabled)
		CancelActiveMeasurements(CancelMode::RunningOnly);

	SyncProfilerCaptureModeLimit();
	if (IsAnyFeatureCostMeasurementRunning()) {
		const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(
			{}, Profiler::CaptureMode::WholeFrameOnly);
		const double currentTime = ImGui::GetTime();
#ifdef DEVBENCH_BRIDGE_ENABLED
		RecordMeasurementTrace(timing, currentTime);
#endif
		for (auto& [shortName, state] : g_costMeasurementStates) {
			if (!IsFeatureCostMeasurementRunning(state))
				continue;
			auto* feature = FindFeatureByShortName(shortName);
			if (!feature || !feature->loaded) {
				FailFeatureCostMeasurement(feature, state,
					T(TKEY("error.feature_unavailable"), "Stopped because the measured feature became unavailable."));
				continue;
			}
			UpdateFeatureCostMeasurement(feature, state, timing, currentTime);
		}
	}
	SyncProfilerCaptureModeLimit();
	UpdateFeatureCostBatch(ImGui::GetTime());
	UpdateMonoSweep(ImGui::GetTime());
	if (!IsAnyFeatureCostMeasurementLocked() && !g_featureCostBatch.active && !g_monoSweep.Active()) {
		g_closedMenuMeasurement = false;
		g_vanityCameraSuppression.Release();
		RestoreProfilerStateAfterPerformanceTuning();
		if (g_reopenMenuAfterMeasurement && globals::menu) {
			globals::menu->OpenMenu();
		}
	}
}

void PerformanceTuningRenderer::RenderClosedMenuMeasurementOverlay()
{
	if (!g_closedMenuMeasurement && !g_quickScan.controller.Active() && !g_monoSweep.Active())
		return;
	for (const auto& [shortName, state] : g_costMeasurementStates) {
		if (!IsFeatureCostMeasurementRunning(state))
			continue;
		auto* feature = FindFeatureByShortName(shortName);
		const bool comparison = state.leg == FeatureCostMeasurementLeg::Comparison;
		const double comparisonWait = std::max(PerformanceTuning::kComparisonWaitSeconds,
			feature ? feature->GetPerformanceCostMeasurementSettleSeconds(false) : 0.0);
		auto phase = comparison ? PerformanceTuning::RunPhase::WaitingComparison : PerformanceTuning::RunPhase::WaitingCurrent;
		if (state.stage == FeatureCostMeasurementStage::Measuring || state.stage == FeatureCostMeasurementStage::Draining)
			phase = comparison ? PerformanceTuning::RunPhase::MeasuringComparison : PerformanceTuning::RunPhase::MeasuringCurrent;
		else if (state.stage == FeatureCostMeasurementStage::Restoring)
			phase = PerformanceTuning::RunPhase::Restoring;
		const double remaining = PerformanceTuning::GetRemainingSeconds(phase,
			std::max(0.0, ImGui::GetTime() - state.stageStartTime),
			GetFeatureCostSampleWindow(state).sampledDurationMs, comparisonWait);
		const float progress = static_cast<float>(std::clamp(
			1.0 - remaining / PerformanceTuning::GetExpectedRunSeconds(comparisonWait), 0.0, 0.99));

		const auto* viewport = ImGui::GetMainViewport();
		if (!viewport)
			return;
		const float scale = Util::GetUIScale();
		const float horizontalPadding = 24.0f * scale;
		const float overlayWidth = std::min(360.0f * scale,
			std::max(220.0f * scale, viewport->WorkSize.x - horizontalPadding * 2.0f));
		ImGui::SetNextWindowPos(
			ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 32.0f * scale),
			ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(overlayWidth, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.92f);
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin("ActualFeatureCostProgress", nullptr, flags)) {
			ImGui::Text(T(TKEY("progress.measuring"), "Measuring %s"),
				feature ? feature->GetDisplayName().c_str() : shortName.c_str());
			ImGui::TextColored(Util::Colors::GetWarning(), "%s",
				T(TKEY("progress.keep_still"), "Keep still until measurement completes."));
			const std::string progressText = fmt::format(
				fmt::runtime(T(TKEY("progress.remaining"), "{:.0f} seconds remaining")), std::ceil(remaining));
			ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), progressText.c_str());
		}
		ImGui::End();
		return;
	}
	const auto* viewport = ImGui::GetMainViewport();
	if (!viewport)
		return;
	const float scale = Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
		viewport->WorkPos.y + 32.0f * scale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(std::min(420.0f * scale, viewport->WorkSize.x - 48.0f * scale), 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);
	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("PerformanceMeasurementProgress", nullptr, flags)) {
		ImGui::TextColored(Util::Colors::GetWarning(), "%s", T(TKEY("progress.keep_still"), "Keep still until measurement completes."));
		if (g_quickScan.controller.Active()) {
			if (g_quickScan.controller.phase == PerformanceQuickScan::Phase::Capturing) {
				ImGui::Text(T(TKEY("quick_scan.progress"), "Quick scan: %u / %u frames"), g_quickScan.resolvedFrames, PerformanceQuickScan::kCaptureFrames);
				ImGui::ProgressBar(static_cast<float>(g_quickScan.resolvedFrames) / PerformanceQuickScan::kCaptureFrames);
			} else {
				const double remaining = std::max(0.0, PerformanceQuickScan::kSettleSeconds - (ImGui::GetTime() - g_quickScan.controller.readySince));
				ImGui::Text(T(TKEY("quick_scan.settling"), "Quick scan settling: %.0f seconds"), std::ceil(remaining));
			}
		} else if (g_monoSweep.Active()) {
			ImGui::Text(T(TKEY("sweep.progress"), "Upscaling sweep: %zu / %zu cases"), g_monoSweep.index, g_monoSweep.cases.size());
			ImGui::TextUnformatted(g_monoSweep.phase == MonoSweepPhase::Restoring ?
				T(TKEY("progress.restoring"), "Restoring original settings...") :
				T(TKEY("progress.waiting_profile"), "Waiting for the next profile to become ready..."));
			if (!g_monoSweep.failure.empty())
				ImGui::TextWrapped("%s", g_monoSweep.failure.c_str());
		} else if (IsAnyFeatureCostMeasurementLocked()) {
			ImGui::TextUnformatted(T(TKEY("progress.restoring"), "Restoring original settings..."));
		} else if (g_featureCostBatch.active) {
			ImGui::Text(T(TKEY("batch.progress"), "Feature comparisons: %zu / %zu"), g_featureCostBatch.nextFeatureIndex, g_featureCostBatch.features.size());
			ImGui::Text(T(TKEY("batch.cooldown"), "Next comparison in %.0f seconds"), std::ceil(GetFeatureCostRestartCooldownRemaining(ImGui::GetTime())));
		}
	}
	ImGui::End();
}

void PerformanceTuningRenderer::Render()
{
	MenuUI::SettingsPage page("PerformanceTuning", {
		{ "quick_scan", T(TKEY("page.quick_scan"), "Quick scan"), T(TKEY("page.quick_scan_description"), "Capture feature timings together without changing settings."), T(TKEY("page.quick_scan_summary"), "Find expensive rendering work") },
		{ "compare", T(TKEY("page.compare"), "Compare feature costs"), T(TKEY("page.compare_description"), "Measure enabled features against Off or None in the current scene."), T(TKEY("page.compare_summary"), "SE frame timing, coverage and repeatability") }
	}, T(TKEY("page.title"), "Performance tuning"), T(TKEY("page.guidance"), "Scan feature timings or measure individual on/off savings."));
	if (page.Is("quick_scan")) {
		RenderQuickScan();
		return;
	}
	if (!page.Is("compare"))
		return;
	RenderFeatureSetMeasurement(BuildPerformanceFeatureList());
	ImGui::Separator();
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

	// Keep UI work constant and minimal for both captures. In particular,
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
				g_costMeasurementStates[runningFeature->GetShortName()]);
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
					selectedFeature,
					selectedCostState,
					currentUiState);
			}

			const bool sceneControlled =
				IsFeatureControlledBySceneSettings(selectedFeature);
			const bool applicable =
				selectedFeature->IsPerformanceTuningApplicable();
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
			} else if (!applicable) {
				const char* reason =
					selectedFeature->GetPerformanceTuningApplicabilityReason();
				Util::Text::WrappedWarning(
					reason ?
						reason :
						T(
							TKEY("not_applicable"),
							"This feature has no active work in the current scene."));
			}

			const bool startMeasurement = RenderFeatureCostMeasurement(
				selectedFeature,
				selectedCostState);
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
				// comparison. The cost test owns its own transition gates and raw sample
				// windows, so discard any stale preview instead of making it a
				// prerequisite for starting an actual cost measurement.
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
	g_featureCostBatch.active = false;
	if (g_quickScan.controller.Active())
		FinishQuickScan(PerformanceQuickScan::Phase::Cancelled, T(TKEY("error.cancelled"), "Measurement cancelled."), false);
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

	if (g_monoSweep.Active()) {
		BeginMonoSweepRestore(MonoSweepPhase::Cancelled, T(TKEY("error.cancelled"), "Measurement cancelled."));
		UpdateMonoSweep(ImGui::GetTime());
	}

	if (mode == CancelMode::ClearSession) {
		g_closedMenuMeasurement = false;
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
	g_vanityCameraSuppression.Release();
	SyncProfilerCaptureModeLimit();
	RestoreProfilerStateAfterPerformanceTuning();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return g_quickScan.controller.Active() || g_featureCostBatch.active || g_monoSweep.Active() || g_closedMenuMeasurement || IsAnyFeatureCostMeasurementLocked();
}

bool PerformanceTuningRenderer::PrepareForSceneUpdate()
{
	const char* reason = T(TKEY("error.scene_changed"), "Stopped because the scene changed.");
	const bool restored = PrepareFeatureCostMeasurementsForSceneChange(false, reason);
	if (g_quickScan.controller.Active() && GetMeasurementCellId() != g_quickScan.cell)
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, reason, false);
	if (g_monoSweep.Active() && HasMaterialSceneChange(g_monoSweep.scene, CaptureSceneFingerprint())) {
		g_monoSweep.reopenMenu = false;
		BeginMonoSweepRestore(MonoSweepPhase::Failed, reason);
		return restored && RelinquishMonoSweepForConfiguration();
	}
	return restored;
}

bool PerformanceTuningRenderer::PrepareForSceneSettingsTransition()
{
	const char* reason = T(TKEY("error.scene_owned"), "Stopped because scene-specific settings took control of this feature.");
	if (g_quickScan.controller.Active())
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, reason, false);
	g_featureCostBatch.active = false;
	const bool restored = PrepareFeatureCostMeasurementsForSceneChange(true, reason);
	if (g_monoSweep.Active()) {
		g_monoSweep.reopenMenu = false;
		BeginMonoSweepRestore(MonoSweepPhase::Failed, reason);
		return restored && RelinquishMonoSweepForConfiguration();
	}
	return restored;
}


void PerformanceTuningRenderer::NotifyOverviewInactive()
{
	if (!HasActiveMeasurements())
		RestoreProfilerStateAfterPerformanceTuning();
}

void PerformanceTuningRenderer::RenderFeatureEnabledControl(Feature* a_feature)
{
	const bool supported = a_feature->SupportsPerformanceCostMeasurement();
	bool enabled = supported ? a_feature->IsPerformanceToggleEnabled() : a_feature->loaded;
	const auto* reason = GetFeatureToggleBlockReason(a_feature);
	const bool busy = HasActiveMeasurements();
	{
		auto guard = Util::DisableGuard(!a_feature->loaded || !supported || reason || busy);
		if (Util::Widgets::Checkbox(T("menu.performance_tuning.shared.enabled", "Enabled"), &enabled)) {
			const bool applied = SetRuntimeFeatureEnabled(a_feature, enabled);
			g_featureCostUiMessage = applied ? "" : T(TKEY("toggle.failed"), "This feature could not change state. Check its settings and runtime requirements.");
		}
	}
	Util::AddTooltip(reason ? reason : busy ? T(TKEY("toggle.busy"), "Wait for the current measurement to finish.") :
		!supported ? T(TKEY("toggle.startup_only"), "No runtime switch. Change startup in Advanced > Startup, save settings and restart.") :
		T(TKEY("toggle.tooltip"), "Turns this feature on or off while keeping your current tuning."));
}

void PerformanceTuningRenderer::RenderMeasurementSuite(Feature* feature)
{
	if (feature && !feature->SupportsPerformanceCostMeasurement())
		return;
	CaptureProfilerStateForPerformanceTuning();
	SyncProfilerCaptureModeLimit();
	const auto features = feature ? std::vector<Feature*>{ feature } : BuildPerformanceFeatureList();
	const auto captureMode = IsAnyFeatureCostMeasurementRunning() ? Profiler::CaptureMode::WholeFrameOnly : Profiler::CaptureMode::DetailedPasses;
	const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(BuildPerformanceFeaturePrefixes(features), captureMode);
	RenderTopPerformanceCounters(timing, g_highlightState);
	g_lastTimingSummary = timing;
	if (!feature) {
		RenderFeatureSetMeasurement(features);
		return;
	}
	const auto name = feature->GetShortName();
	ImGui::PushID(name.c_str());
	const SKSE::stl::scope_exit pop([] { ImGui::PopID(); });
	RenderFeatureEnabledControl(feature);
	if (RenderFeatureCostMeasurement(feature, g_costMeasurementStates[name]))
		StartFeatureCostMeasurement(feature, g_costMeasurementStates[name], timing, ImGui::GetTime());
	if (!HasActiveMeasurements() && ProfilingRenderer::CanProfileFeature(name))
		ProfilingRenderer::RenderFeaturePerformanceSummary(name);
}

void PerformanceTuningRenderer::NotifyFeatureSettingsChanged(Feature* feature)
{
	if (g_quickScan.controller.Active())
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, T(TKEY("quick_scan.settings_changed"), "Feature settings changed during the quick scan."));
	if (feature && !HasActiveMeasurements())
		InvalidateFeatureCostResults();
}

bool PerformanceTuningRenderer::NotifyConfigurationChanging()
{
	g_monoSweep.reopenMenu = false;
	CancelActiveMeasurements();
	if (IsAnyFeatureCostMeasurementLocked() || !RelinquishMonoSweepForConfiguration())
		return false;
	g_disabledFeatureConfigurations.clear();
	g_quickScan = {};
	g_featureCostBatch = {};
	return true;
}

bool PerformanceTuningRenderer::CancelUserMeasurements()
{
	const bool ownsScan = g_quickScan.controller.Active() && !g_quickScan.devBenchOwned;
	const bool ownsBatch = g_featureCostBatch.active && !g_featureCostBatch.devBenchOwned;
	const bool ownsCost = std::ranges::any_of(g_costMeasurementStates, [](const auto& entry) {
		return IsFeatureCostMeasurementLocked(entry.second) && !entry.second.devBenchOwned;
	});
	if (!ownsScan && !ownsBatch && !ownsCost)
		return false;
	CancelActiveMeasurements(CancelMode::RunningOnly);
	return true;
}


#ifdef DEVBENCH_BRIDGE_ENABLED
nlohmann::json PerformanceTuningRenderer::StartDevBenchFeatureCostMeasurement(std::string_view name)
{
	json response = { { "action", "start_feature_cost" }, { "accepted", false }, { "feature", name } };
	auto* feature = FindFeatureByShortName(std::string(name));
	const auto* error = GetFeatureCostStartError(ImGui::GetTime());
	if (!error && (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement())) error = "feature_unavailable";
	if (!error && GetFeatureToggleBlockReason(feature)) error = "feature_controlled";
	if (!error && (!feature->IsPerformanceCostMeasurementEnabled() || !feature->IsPerformanceTuningApplicable())) error = "feature_not_ready";
	if (!error) {
		g_measurementTrace.clear();
		g_traceLast = -1.0;
		CaptureProfilerStateForPerformanceTuning();
		const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary({}, Profiler::CaptureMode::WholeFrameOnly);
		auto& state = g_costMeasurementStates[feature->GetShortName()];
		StartFeatureCostMeasurement(feature, state, timing, ImGui::GetTime(), true, true);
		if (!IsFeatureCostMeasurementRunning(state)) {
			error = "measurement_start_failed";
			RestoreProfilerStateAfterPerformanceTuning();
		}
	}
	response["accepted"] = error == nullptr;
	if (error) response["errorCode"] = error;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchQuickScan()
{
	const auto* error = StartQuickScan(true);
	json response = { { "action", "start_quick_scan" }, { "accepted", error == nullptr } };
	if (error) response["errorCode"] = error;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchFeatureCostBatch()
{
	const auto* error = StartFeatureCostBatch(true);
	json response = { { "action", "start_feature_costs" }, { "accepted", error == nullptr } };
	if (!error) { g_measurementTrace.clear(); g_traceLast = -1.0; }
	else response["errorCode"] = error;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::SetDevBenchFeatureEnabled(std::string_view name, bool enabled)
{
	json response = { { "action", "set_feature_enabled" }, { "accepted", false }, { "feature", name } };
	auto* feature = FindFeatureByShortName(std::string(name));
	if (const auto* error = GetFeatureCostStartError(ImGui::GetTime())) response["errorCode"] = error;
	else if (!SetRuntimeFeatureEnabled(feature, enabled)) response["errorCode"] = "feature_toggle_failed";
	else response["accepted"] = true;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchUpscalingCostSweep(std::string_view matrix, std::string_view preset)
{
	json response = { { "action", "start_upscaling_sweep" }, { "accepted", false } };
	const auto reject = [&](const char* reason) {
		response["errorCode"] = reason;
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
		return response;
	};
	if (matrix != "auto" && matrix != "nvidia" && matrix != "amd") return reject("invalid_matrix");
	if (const auto* error = GetFeatureCostStartError(ImGui::GetTime())) return reject(error);
	auto& upscaling = globals::features::upscaling;
	if (!upscaling.loaded || !upscaling.SupportsPerformanceCostMeasurement() || GetFeatureToggleBlockReason(&upscaling)) return reject("feature_controlled");
	const bool amd = matrix == "amd" || (matrix == "auto" && Upscaling::fidelityFX.IsAmdAdapterDetected());
	if (amd && !Upscaling::fidelityFX.IsAmdAdapterDetected()) return reject("amd_adapter_required");
	if (amd && (!Upscaling::fidelityFX.IsRuntimeFsr4AutoEligible() || !Upscaling::fidelityFX.IsRuntimeFsr4Available())) return reject("fsr4_unavailable");
	if (!amd && (!Upscaling::fidelityFX.IsNvidiaAdapterDetected() || !Upscaling::streamline.featureCheckComplete || !Upscaling::streamline.featureDLSS)) return reject("dlss_unavailable");
	uint32_t presetId = upscaling.GetMonoConfiguredProfile().dlssPreset;
	constexpr std::array<std::string_view, 6> presets{ "J", "K", "L", "M", "F", "E" };
	if (!amd && preset.empty()) {
		response["promptRequired"] = true;
		response["requiredParameter"] = "dlssPreset";
		response["choices"] = presets;
		return reject("dlss_preset_required");
	}
	if (!preset.empty()) {
		if (amd) return reject("dlss_preset_not_applicable");
		const auto found = std::ranges::find(presets, preset);
		if (found == presets.end()) return reject("invalid_dlss_preset");
		presetId = static_cast<uint32_t>(found - presets.begin());
	}
	if (presetId > Upscaling::kDLSSPresetMaxIndex) return reject("invalid_dlss_preset");
	MonoSweepState pending;
	pending.devBenchOwned = true;
	pending.matrix = amd ? "amd" : "nvidia";
	pending.originalState = upscaling.CapturePerformanceCostMeasurementState();
	pending.originalProfile = upscaling.GetMonoConfiguredProfile();
	pending.originalFrameGeneration = upscaling.CaptureFrameGenerationSettingsForPerformanceMeasurement();
	pending.scene = CaptureSceneFingerprint();
	pending.reopenMenu = globals::menu->IsEnabled;
	const auto add = [&](Upscaling::UpscaleMethod method, uint32_t quality, bool automatic, std::string_view id) {
		pending.cases.push_back({ fmt::format("{}_{}", id, quality), fmt::format("{} / {:.0f}%", id, Upscaling::GetQualityModeResolutionScale(quality) * 100.0f), { method, quality, presetId, automatic } });
	};
	add(Upscaling::UpscaleMethod::kTAA, 0, false, "taa");
	for (uint32_t quality = 0; quality <= Upscaling::kQualityModeMaxIndex; ++quality)
		add(amd ? Upscaling::UpscaleMethod::kFSR : Upscaling::UpscaleMethod::kDLSS, quality, amd, amd ? "fsr_automatic" : "dlss");
	for (uint32_t quality = 0; quality <= Upscaling::kQualityModeMaxIndex; ++quality)
		add(Upscaling::UpscaleMethod::kFSR, quality, false, "fsr3");
	if (!g_vanityCameraSuppression.Acquire()) return reject("camera_suppression_failed");
	CaptureProfilerStateForPerformanceTuning();
	g_monoSweep = std::move(pending);
	g_monoSweep.phase = MonoSweepPhase::Staging;
	g_monoSweep.restorePending = true;
	g_closedMenuMeasurement = true;
	g_reopenMenuAfterMeasurement = g_monoSweep.reopenMenu;
	g_measurementTrace.clear(); g_traceLast = -1.0;
	upscaling.DisableFrameGenerationForPerformanceMeasurement();
	if (!StageMonoSweepCase()) {
		BeginMonoSweepRestore(MonoSweepPhase::Failed, "The next upscaling profile could not be staged.");
		UpdateMonoSweep(ImGui::GetTime());
		return reject("profile_rejected");
	}
	globals::menu->CloseMenu();
	response["accepted"] = true;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::GetDevBenchMeasurementStatus(uint64_t afterSequence, size_t maximumSamples)
{
	return BuildDevBenchMeasurementStatus(afterSequence, maximumSamples);
}

nlohmann::json PerformanceTuningRenderer::CancelDevBenchMeasurements()
{
	const bool ownsCost = std::ranges::any_of(g_costMeasurementStates, [](const auto& item) { return item.second.devBenchOwned && IsFeatureCostMeasurementLocked(item.second); });
	const bool owned = ownsCost || (g_quickScan.controller.Active() && g_quickScan.devBenchOwned) ||
		(g_featureCostBatch.active && g_featureCostBatch.devBenchOwned) || (g_monoSweep.Active() && g_monoSweep.devBenchOwned);
	if (owned) CancelActiveMeasurements(CancelMode::RunningOnly);
	return { { "action", "cancel" }, { "accepted", owned }, { "status", BuildDevBenchMeasurementStatus(0, 128) } };
}
#endif

#undef I18N_KEY_PREFIX
