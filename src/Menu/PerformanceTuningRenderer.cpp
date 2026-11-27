#include "PerformanceTuningRenderer.h"
#include "BuildProvenance.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/SettingsPage.h"
#include "Menu/SettingsSearch.h"
#include "PerformanceFpsVR.h"
#include "PerformancePage.h"
#include "PerformanceResultsUI.h"
#include <cstring>
#include <intrin.h>
#include <set>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Features/PerformanceOverlay/ABTesting/ABComparisonRunner.h"
#endif
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceQuickScan.h"
#include "Menu/PerformanceTuningController.h"
#include "Menu/PerformanceTuningStatistics.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "ShaderCache.h"
#include "Utils/Form.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/UI.h"
#include "Utils/VanityCamera.h"
#include "Utils/WinApi.h"

namespace
{
	static_assert(PerformanceQuickScan::kCaptureFrames <= Profiler::kHistorySize);
	constexpr double kFeatureCostMeasurementSeconds = 3.0;
	constexpr double kFeatureCostMeasurementMilliseconds = kFeatureCostMeasurementSeconds * 1000.0;
	constexpr double kFeatureCostIntervalMilliseconds = 500.0;
	constexpr double kFeatureCostInitialWaitSeconds = 2.0;
	constexpr double kFeatureCostComparisonWaitSeconds = 3.0;
	constexpr double kFeatureCostRestoreWaitSeconds = 1.0;
	constexpr double kFeatureCostRestartCooldownSeconds = 5.0;
	constexpr double kFeatureCostMaximumRunSeconds = 45.0;
	constexpr std::size_t kFeatureCostMaximumMissingMetricSamples = 2;
#ifdef DEVBENCH_BRIDGE_ENABLED
	constexpr double kFeatureCostTraceIntervalSeconds = 0.1;
	constexpr std::size_t kFeatureCostTraceCapacity = 8192;
	constexpr std::size_t kFeatureCostMaximumTracePageSize = 512;
#endif

	constexpr std::size_t kFeatureCostMeasurementBlockCount =
		static_cast<std::size_t>(kFeatureCostMeasurementMilliseconds / kFeatureCostIntervalMilliseconds);
	static_assert(
		kFeatureCostMeasurementBlockCount == 6 &&
		kFeatureCostMeasurementBlockCount * kFeatureCostIntervalMilliseconds == kFeatureCostMeasurementMilliseconds);

	constexpr std::array<std::string_view, 23> kPerformanceFeatureOrder = {
		"Upscaling",
		"VR",
		"AdaptiveBrightness",
		"LinearLighting",
		"ScreenSpaceShadows",
		"ScreenSpaceGI",
		"LightLimitFix",
		"Skylighting",
		"CloudShadows",
		"TerrainBlending",
		"TerrainShadows",
		"VolumetricLighting",
		"VolumetricShadows",
		"Wetterness",
		"SubsurfaceScattering",
		"Skin",
		"TruePBR",
		"ExtendedMaterials",
		"DetailMaps",
		"FoliageLighting",
		"GrassOptimizations",
		"GrassLighting",
		"GrassCollision"
	};

	using FeatureCostMoments = PerformanceTuningStatistics::Moments;

	struct FeatureCostMetricSample
	{
		std::array<FeatureCostMoments, kFeatureCostMeasurementBlockCount> blocks{};
		std::size_t missingSampleCount = 0;
		bool accumulationValid = true;
	};

	struct FeatureCostSample
	{
		double sampledDurationMs = 0.0;
		FeatureCostMetricSample frame;
		FeatureCostMetricSample gameGpu;
		FeatureCostMetricSample gameCpu;
		uint32_t lastFrameCount = 0;
		std::uint64_t vrTimingEpoch = 0;
		bool vrWindowInitialized = false;
		std::deque<Util::FlatFrameTiming::PendingSample<kFeatureCostMeasurementBlockCount>> pendingFlatSamples;
	};

	struct FeatureCostMetricDelta
	{
		float value = 0.0f;
		float standardError = 0.0f;
		float pValue = 1.0f;
		double costPercent = 0.0;
		double currentValue = 0.0;
		double comparisonValue = 0.0;
		bool hasCostPercent = false;
		std::size_t missingSampleCount = 0;
		bool available = false;
		bool hasStandardError = false;
		bool significant = false;
	};

	struct FeatureCostDelta
	{
		FeatureCostMetricDelta frame;
		FeatureCostMetricDelta fps;
		FeatureCostMetricDelta gameGpu;
		FeatureCostMetricDelta gameCpu;
	};

	enum class FeatureCostMeasurementPhase
	{
		Idle,
		AwaitingMenuClose,
		PreparingCurrent,
		MeasuringCurrent,
		PreparingTest,
		MeasuringTest,
		Restoring,
		Complete
	};

	enum class FeatureCostSampleResult
	{
		Pending,
		Complete,
		Interrupted
	};

	struct FeatureCostMeasurementState
	{
		FeatureCostMeasurementPhase phase = FeatureCostMeasurementPhase::Idle;
		PerformanceTuningController::FeatureCostWaits waits{ kFeatureCostInitialWaitSeconds, kFeatureCostComparisonWaitSeconds, kFeatureCostRestoreWaitSeconds };
		json originalState;
		RE::FormID cellFormId = 0;
		bool testStateApplied = false;
		bool devBenchOwned = false;
		bool reopenMenuOnCompletion = false;
		double phaseDeadlineTime = 0.0;
		double phaseStartTime = 0.0;
		double runStartTime = 0.0;
		FeatureCostSample currentSample;
		FeatureCostSample testSample;
		FeatureCostDelta delta;
		std::string failureMessage;
	};

	enum class UpscalingCostSweepPhase
	{
		Idle,
		AwaitingMenuClose,
		Measuring,
		InterCaseCooldown,
		RestoringOriginal,
		Complete,
		Failed,
		Cancelled
	};

	enum class UpscalingCostSweepMatrix
	{
		Nvidia,
		Amd
	};

	struct UpscalingCostSweepCase
	{
		std::string id;
		std::string label;
		json profile;
		FeatureCostDelta delta;
	};

	struct UpscalingCostSweepState
	{
		UpscalingCostSweepPhase phase = UpscalingCostSweepPhase::Idle;
		UpscalingCostSweepPhase terminalPhase = UpscalingCostSweepPhase::Complete;
		UpscalingCostSweepMatrix matrix = UpscalingCostSweepMatrix::Nvidia;
		uint32_t dlssPreset = Upscaling::kDLSSPresetK;
		json originalState;
		bool mainMenuWasOpen = false;
		bool editorWasOpen = false;
		double runStartTime = 0.0;
		double phaseStartTime = 0.0;
		std::size_t currentCaseIndex = 0;
		std::vector<UpscalingCostSweepCase> cases;
		std::vector<UpscalingCostSweepCase> results;
		std::string failureMessage;
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	struct FeatureCostTraceSample
	{
		std::uint64_t sequence = 0;
		double runElapsedMs = 0.0;
		double phaseElapsedMs = 0.0;
		std::uint32_t frameCount = 0;
		std::uint32_t sampleFrameCount = 0;
		std::uint64_t samplePresentId = 0;
		bool flatTiming = false;
		bool vrTiming = false;
		std::uint64_t vrTimingEpoch = 0;
		double compositorSystemTime = 0.0;
		float sceneGpuMs = 0.0f;
		float poseWaitMs = 0.0f;
		bool hasSceneGpu = false;
		bool hasPoseWait = false;
		float frameMs = 0.0f;
		float gameGpuMs = 0.0f;
		float gameCpuMs = 0.0f;
		bool hasFrame = false;
		bool hasGameGpu = false;
		bool hasGameCpu = false;
		std::string phase;
		std::string caseId;
	};

#endif

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static UpscalingCostSweepState g_upscalingCostSweep;
#ifdef DEVBENCH_BRIDGE_ENABLED
	static std::deque<FeatureCostTraceSample> g_featureCostTrace;
	static std::uint64_t g_featureCostTraceNextSequence = 1;
	static double g_featureCostTraceLastTime = -1.0;
#endif

	static double g_costMeasurementRestartAllowedTime = 0.0;
	json g_savedRun;
	double g_savedRunStart = 0;
	std::set<std::string> g_savedRows;
	void FinishSavedRun(std::string status, std::string failure = {});
	void CaptureSavedFeature(std::string_view name, const FeatureCostMeasurementState& state);
	void UpdateSavedRun();

	static Util::VanityCameraSuppressionLease g_featureCostVanityCameraSuppression;
	static bool g_profilerStateCaptured = false;
	static bool g_profilerWasUserEnabled = false;
	using DisabledFeatureConfiguration = PerformanceTuningController::DisabledConfiguration;

	struct FeatureCostBatchState
	{
		bool active = false;
		bool devBenchOwned = false;
		bool reopenMenuOnCompletion = false;
		std::size_t nextFeatureIndex = 0;
		double started = 0.0;
		double expectedSeconds = 0.0;
		RE::FormID cellFormId = 0;
		std::vector<std::string> features;
		std::string failureMessage;
	};

	static std::unordered_map<std::string, std::optional<DisabledFeatureConfiguration>> g_disabledFeatureConfigurations;
	static FeatureCostBatchState g_featureCostBatch;
	static std::string g_featureCostUiMessage;
	static Feature* failedToggleFeature = nullptr;
	static std::string g_featureToggleFailure;
	static int tuningView = 0;

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
		bool fpsVrPrepared = false;
		PerformanceQuickScan::CaptureOwnership<Profiler> capture;
		PerformanceQuickScan::FrameTimingCapture wholeFrame;
		RE::FormID cell = 0;
		std::uint64_t captureId = 0;
		std::uint32_t resolvedFrames = 0;
		std::uint32_t invalidGpuFrames = 0;
		std::uint32_t invalidCpuFrames = 0;
		std::uint32_t gpuRefusals = 0;
		std::uint32_t cpuRefusals = 0;
		double captureSeconds = 0.0;
		double captureStarted = 0.0;
		std::string captureRequestedUtc, captureResolvedUtc;
		double startSteamVrSeconds = 0, endSteamVrSeconds = 0;
		std::uint64_t steamVrEpoch = 0;
		json settings;
		std::vector<QuickScanRow> rows;
		std::string failure;
	};
	static QuickScanState g_quickScan;
	static std::shared_ptr<PerformanceFpsVR::Session> g_scanFpsVR;

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

	int GetDirectionFromFeatureCostFrameTimeDelta(float deltaMs)
	{
		if (deltaMs == 0.0f)
			return 0;

		return deltaMs > 0.0f ? 1 : -1;
	}

	int GetDirectionFromFeatureCostFpsDelta(float deltaFps)
	{
		if (deltaFps == 0.0f)
			return 0;

		return deltaFps > 0.0f ? -1 : 1;
	}

	bool IsFeatureCostMeasurementActive(const FeatureCostMeasurementState& state)
	{
		return state.phase != FeatureCostMeasurementPhase::Idle &&
		       state.phase != FeatureCostMeasurementPhase::Complete;
	}

	bool IsAnyFeatureCostMeasurementActive()
	{
		for (const auto& [_, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementActive(state))
				return true;
		}

		return false;
	}

	bool IsUpscalingCostSweepRunning()
	{
		return g_upscalingCostSweep.phase == UpscalingCostSweepPhase::AwaitingMenuClose ||
		       g_upscalingCostSweep.phase == UpscalingCostSweepPhase::Measuring ||
		       g_upscalingCostSweep.phase == UpscalingCostSweepPhase::InterCaseCooldown ||
		       g_upscalingCostSweep.phase == UpscalingCostSweepPhase::RestoringOriginal;
	}

	const char* GetFeatureCostPhaseName(FeatureCostMeasurementPhase phase)
	{
		switch (phase) {
		case FeatureCostMeasurementPhase::AwaitingMenuClose:
			return "awaiting_menu_close";
		case FeatureCostMeasurementPhase::PreparingCurrent:
			return "initial_cooldown";
		case FeatureCostMeasurementPhase::MeasuringCurrent:
			return "measuring_current";
		case FeatureCostMeasurementPhase::PreparingTest:
			return "comparison_wait";
		case FeatureCostMeasurementPhase::MeasuringTest:
			return "measuring_none";
		case FeatureCostMeasurementPhase::Restoring:
			return "restoring";
		case FeatureCostMeasurementPhase::Complete:
			return "complete";
		case FeatureCostMeasurementPhase::Idle:
		default:
			return "idle";
		}
	}

	const char* GetUpscalingCostSweepPhaseName(UpscalingCostSweepPhase phase)
	{
		switch (phase) {
		case UpscalingCostSweepPhase::AwaitingMenuClose:
			return "awaiting_menu_close";
		case UpscalingCostSweepPhase::Measuring:
			return "measuring";
		case UpscalingCostSweepPhase::InterCaseCooldown:
			return "inter_case_cooldown";
		case UpscalingCostSweepPhase::RestoringOriginal:
			return "restoring_original";
		case UpscalingCostSweepPhase::Complete:
			return "complete";
		case UpscalingCostSweepPhase::Failed:
			return "failed";
		case UpscalingCostSweepPhase::Cancelled:
			return "cancelled";
		case UpscalingCostSweepPhase::Idle:
		default:
			return "idle";
		}
	}

	const char* GetUpscalingCostSweepMatrixName(UpscalingCostSweepMatrix matrix)
	{
		switch (matrix) {
		case UpscalingCostSweepMatrix::Amd:
			return "amd";
		case UpscalingCostSweepMatrix::Nvidia:
		default:
			return "nvidia";
		}
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	std::string_view GetUpscalingCostSweepTraceCaseId()
	{
		const auto& sweep = g_upscalingCostSweep;
		if (sweep.cases.empty())
			return {};
		if (sweep.phase == UpscalingCostSweepPhase::InterCaseCooldown &&
			sweep.currentCaseIndex > 0) {
			return sweep.cases[sweep.currentCaseIndex - 1].id;
		}
		if (sweep.currentCaseIndex < sweep.cases.size())
			return sweep.cases[sweep.currentCaseIndex].id;
		return sweep.cases.back().id;
	}

#endif
	json GetDLSSPresetChoicesJson()
	{
		return json::array({ "J", "K", "L", "M", "F", "E" });
	}

	double GetFeatureCostRestartCooldownRemaining(double currentTime)
	{
		return std::max(0.0, g_costMeasurementRestartAllowedTime - currentTime);
	}

	void StartFeatureCostRestartCooldown(double currentTime)
	{
		const bool continuingBatch = g_featureCostBatch.active && g_featureCostBatch.nextFeatureIndex < g_featureCostBatch.features.size();
		g_costMeasurementRestartAllowedTime = currentTime + (continuingBatch ? PerformanceTuningController::kFeatureBatchCooldownSeconds : kFeatureCostRestartCooldownSeconds);
	}

	struct UpscalingCostSweepReadiness
	{
		bool idle = false;
		bool vr = false;
		bool inGame = false;
		bool menuAvailable = false;
		bool measurementSupported = false;
		bool restartCooldownComplete = false;

		[[nodiscard]] bool Ready() const
		{
			return idle && vr && inGame && menuAvailable &&
			       measurementSupported && restartCooldownComplete;
		}
	};

	UpscalingCostSweepReadiness CaptureUpscalingCostSweepReadiness(double currentTime)
	{
		return {
			.idle = !PerformanceTuningRenderer::HasActiveMeasurements(),
			.vr = globals::game::isVR,
			.inGame = globals::state &&
			          !globals::state->isMainMenuOpen &&
			          !globals::state->isLoadingMenuOpen &&
			          RE::PlayerCharacter::GetSingleton(),
			.menuAvailable = globals::menu != nullptr,
			.measurementSupported = globals::features::upscaling.SupportsPerformanceCostMeasurement(),
			.restartCooldownComplete = GetFeatureCostRestartCooldownRemaining(currentTime) <= 0.0,
		};
	}

	const char* GetUpscalingCostSweepReadinessError(const UpscalingCostSweepReadiness& readiness)
	{
		if (!readiness.idle)
			return "measurement_active";
		if (!readiness.vr)
			return "skyrim_vr_required";
		if (!readiness.inGame)
			return "in_game_state_required";
		if (!readiness.menuAvailable)
			return "menu_unavailable";
		if (!readiness.restartCooldownComplete)
			return "restart_cooldown_active";
		if (!readiness.measurementSupported)
			return "upscaling_measurement_unavailable";
		return nullptr;
	}

	bool IsFsr4UpscalingCostSweepAvailable()
	{
		const auto& fidelityFX = Upscaling::fidelityFX;
		return fidelityFX.IsRuntimeFsr4Available() &&
		       !fidelityFX.IsRuntimeUpscalerFailureLatched() &&
		       !fidelityFX.IsRuntimeFsr4FailureLatched() &&
		       (!fidelityFX.HasRuntimeUpscalerSupportCheckResult() ||
				   fidelityFX.IsRuntimeUpscalerSupportConfirmed());
	}

	void SyncFeatureCostVanityCameraSuppression()
	{
		if (PerformanceTuningRenderer::HasActiveMeasurements())
			g_featureCostVanityCameraSuppression.Acquire();
		else
			g_featureCostVanityCameraSuppression.Release();
	}

	bool IsPositiveFiniteTiming(float value)
	{
		return std::isfinite(value) && value > 0.0f;
	}

	bool IsValidFeatureCostTiming(float value)
	{
		return PerformanceTuningStatistics::IsValidTiming(static_cast<double>(value));
	}

	void ResetFeatureCostTrace()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		g_featureCostTrace.clear();
		g_featureCostTraceLastTime = -1.0;
#endif
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void RecordFeatureCostTrace(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime,
		double runStartTime,
		double phaseStartTime,
		std::string_view phase,
		std::string_view caseId)
	{
		if (g_featureCostTraceLastTime >= 0.0 &&
			currentTime - g_featureCostTraceLastTime < kFeatureCostTraceIntervalSeconds) {
			return;
		}

		FeatureCostTraceSample sample;
		sample.sequence = g_featureCostTraceNextSequence++;
		sample.runElapsedMs = std::max(0.0, currentTime - runStartTime) * 1000.0;
		sample.phaseElapsedMs = std::max(0.0, currentTime - phaseStartTime) * 1000.0;
		sample.frameCount = summary.frameCount;
		sample.sampleFrameCount = summary.flatTiming || summary.vrTiming ? summary.sampleFrameCount : summary.frameCount;
		sample.samplePresentId = summary.samplePresentId;
		sample.flatTiming = summary.flatTiming;
		sample.vrTiming = summary.vrTiming;
		sample.vrTimingEpoch = summary.vrTimingEpoch;
		if (summary.vrTiming && !summary.vrSamples.empty()) {
			const auto& source = summary.vrSamples.back();
			sample.compositorSystemTime = source.systemTime;
			sample.sceneGpuMs = source.sceneGpuMs;
			sample.poseWaitMs = source.poseWaitMs;
			sample.hasSceneGpu = source.hasSceneGpu;
			sample.hasPoseWait = source.hasPoseWait;
		}
		sample.hasFrame = summary.hasFrameSample && IsValidFeatureCostTiming(summary.frameSampleMs);
		sample.hasGameGpu = summary.hasGameGpuSample && IsValidFeatureCostTiming(summary.gameGpuSampleMs);
		sample.hasGameCpu = summary.hasGameCpuSample && IsValidFeatureCostTiming(summary.gameCpuSampleMs);
		sample.frameMs = sample.hasFrame ? summary.frameSampleMs : 0.0f;
		sample.gameGpuMs = sample.hasGameGpu ? summary.gameGpuSampleMs : 0.0f;
		sample.gameCpuMs = sample.hasGameCpu ? summary.gameCpuSampleMs : 0.0f;
		sample.phase = phase;
		sample.caseId = caseId;
		g_featureCostTrace.push_back(std::move(sample));
		while (g_featureCostTrace.size() > kFeatureCostTraceCapacity)
			g_featureCostTrace.pop_front();
		g_featureCostTraceLastTime = currentTime;
	}

#endif

	void AddFeatureCostMoment(
		FeatureCostMetricSample& sample,
		std::size_t blockIndex,
		float value,
		double sampleWeight)
	{
		if (blockIndex >= sample.blocks.size() ||
			!PerformanceTuningStatistics::AddMoment(sample.blocks[blockIndex], value, sampleWeight)) {
			sample.accumulationValid = false;
		}
	}

	void RecordMissingFeatureCostSample(FeatureCostMetricSample& sample)
	{
		sample.missingSampleCount = std::min(
			sample.missingSampleCount + 1,
			kFeatureCostMaximumMissingMetricSamples + 1);
	}

	bool TryGetFeatureCostMeanStatistics(
		const FeatureCostMetricSample& sample,
		double& mean,
		double& meanVariance)
	{
		if (!sample.accumulationValid) {
			mean = 0.0;
			meanVariance = 0.0;
			return false;
		}

		return PerformanceTuningStatistics::TryGetBlockMeanStatistics(sample.blocks, mean, meanVariance);
	}

	void SetFeatureCostSignificance(FeatureCostMetricDelta& delta, double standardError)
	{
		const auto significance = PerformanceTuningStatistics::EvaluateSignificance(delta.value, standardError);
		if (!significance.hasStandardError)
			return;

		delta.standardError = static_cast<float>(significance.standardError);
		delta.pValue = static_cast<float>(significance.pValue);
		delta.hasStandardError = true;
		delta.significant = significance.significant;
	}

	struct FeatureCostMetricAnalysis
	{
		FeatureCostMetricDelta delta;
		double currentMean = 0.0;
		double comparisonMean = 0.0;
		double currentMeanVariance = 0.0;
		double comparisonMeanVariance = 0.0;
	};

	FeatureCostMetricAnalysis AnalyzeFeatureCostMetric(
		const FeatureCostMetricSample& current,
		const FeatureCostMetricSample& comparison)
	{
		FeatureCostMetricAnalysis analysis;
		if (!PerformanceTuningStatistics::IsMissingSampleCountWithinLimit(
				current.missingSampleCount,
				comparison.missingSampleCount,
				kFeatureCostMaximumMissingMetricSamples)) {
			return analysis;
		}
		analysis.delta.missingSampleCount =
			current.missingSampleCount + comparison.missingSampleCount;

		if (!TryGetFeatureCostMeanStatistics(
				current,
				analysis.currentMean,
				analysis.currentMeanVariance) ||
			!TryGetFeatureCostMeanStatistics(
				comparison,
				analysis.comparisonMean,
				analysis.comparisonMeanVariance)) {
			return analysis;
		}

		analysis.delta.value = static_cast<float>(analysis.currentMean - analysis.comparisonMean);
		analysis.delta.available = true;
		analysis.delta.currentValue = analysis.currentMean;
		analysis.delta.comparisonValue = analysis.comparisonMean;
		analysis.delta.hasCostPercent = PerformanceTuningStatistics::TryGetCostPercentage(
			analysis.currentMean - analysis.comparisonMean, analysis.currentMean, analysis.delta.costPercent);
		SetFeatureCostSignificance(
			analysis.delta,
			std::sqrt(analysis.currentMeanVariance + analysis.comparisonMeanVariance));
		if (!analysis.delta.hasStandardError)
			analysis.delta.available = false;
		return analysis;
	}

	FeatureCostMetricDelta AnalyzeFeatureCostFps(const FeatureCostMetricAnalysis& frame)
	{
		FeatureCostMetricDelta fps;
		fps.available = frame.delta.available &&
		                frame.currentMean > 0.0 &&
		                frame.comparisonMean > 0.0;
		if (!fps.available)
			return fps;

		const double currentFps = 1000.0 / frame.currentMean;
		const double comparisonFps = 1000.0 / frame.comparisonMean;
		fps.currentValue = currentFps;
		fps.comparisonValue = comparisonFps;
		fps.value = static_cast<float>(currentFps - comparisonFps);
		fps.hasCostPercent = PerformanceTuningStatistics::TryGetCostPercentage(
			comparisonFps - currentFps, comparisonFps, fps.costPercent);
		if (!frame.delta.hasStandardError)
			return fps;

		const double currentDerivative = 1000.0 / (frame.currentMean * frame.currentMean);
		const double comparisonDerivative = 1000.0 / (frame.comparisonMean * frame.comparisonMean);
		const double fpsMeanVariance =
			currentDerivative * currentDerivative * frame.currentMeanVariance +
			comparisonDerivative * comparisonDerivative * frame.comparisonMeanVariance;
		SetFeatureCostSignificance(fps, std::sqrt(fpsMeanVariance));
		if (!fps.hasStandardError)
			fps.available = false;
		return fps;
	}

	bool TryGetDisplayTimingMs(bool hasGameTiming, float gameTimingMs, float& value)
	{
		if (hasGameTiming && IsPositiveFiniteTiming(gameTimingMs)) {
			value = gameTimingMs;
			return true;
		}
		return false;
	}

	bool TryGetDisplayGpuMs(const ProfilingRenderer::PerformanceTimingSummary& summary, float& value)
	{
		return TryGetDisplayTimingMs(summary.hasGameGpu, summary.gameGpuMs, value);
	}

	bool TryGetDisplayCpuMs(const ProfilingRenderer::PerformanceTimingSummary& summary, float& value)
	{
		return TryGetDisplayTimingMs(summary.hasGameCpu, summary.gameCpuMs, value);
	}

	std::vector<std::string> BuildProfilingPrefixesForFeature(const std::string& shortName);
	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state);

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

	void ResolveFlatFeatureCostSamples(FeatureCostSample& sample, const ProfilingRenderer::PerformanceTimingSummary& summary, bool finalize = false)
	{
		Util::FlatFrameTiming::ResolvePending(sample.pendingFlatSamples, summary.flatSamples,
			summary.flatPresentId, summary.flatTimingEpoch, finalize, [&](const auto& pending, const auto* match) {
				const bool hasGpu = match && match->resolved && match->hasGpu;
				const bool hasCpu = match && match->hasCpu;
				if (!hasGpu)
					RecordMissingFeatureCostSample(sample.gameGpu);
				if (!hasCpu)
					RecordMissingFeatureCostSample(sample.gameCpu);
				for (std::size_t block = 0; block < pending.weights.size(); ++block) {
					if (pending.weights[block] <= 0.0)
						continue;
					if (hasGpu)
						AddFeatureCostMoment(sample.gameGpu, block, match->gpuMs, pending.weights[block]);
					if (hasCpu)
						AddFeatureCostMoment(sample.gameCpu, block, match->cpuMs, pending.weights[block]);
				}
			});
	}

	FeatureCostSampleResult AddFeatureCostFrame(
		FeatureCostSample& sample,
		const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		if (summary.frameCount == 0) {
			if (PerformanceTuningStatistics::IsTimingSampleInterrupted(
					sample.lastFrameCount,
					summary.frameCount,
					false)) {
				return FeatureCostSampleResult::Interrupted;
			}
			return FeatureCostSampleResult::Pending;
		}
		if (summary.frameCount == sample.lastFrameCount)
			return FeatureCostSampleResult::Pending;

		const bool validTiming =
			summary.valid &&
			summary.hasFrameSample &&
			IsValidFeatureCostTiming(summary.frameSampleMs);
		if (PerformanceTuningStatistics::IsTimingSampleInterrupted(
				sample.lastFrameCount,
				summary.frameCount,
				validTiming)) {
			return FeatureCostSampleResult::Interrupted;
		}
		sample.lastFrameCount = summary.frameCount;

		if (!validTiming)
			return FeatureCostSampleResult::Pending;

		const double remainingDurationMs = kFeatureCostMeasurementMilliseconds - sample.sampledDurationMs;
		if (remainingDurationMs <= 0.0)
			return FeatureCostSampleResult::Complete;

		const double frameMs = static_cast<double>(summary.frameSampleMs);
		const bool validGameGpuSample =
			summary.hasGameGpuSample && IsValidFeatureCostTiming(summary.gameGpuSampleMs);
		const bool validGameCpuSample =
			summary.hasGameCpuSample && IsValidFeatureCostTiming(summary.gameCpuSampleMs);
		if (!summary.flatTiming && !validGameGpuSample)
			RecordMissingFeatureCostSample(sample.gameGpu);
		if (!summary.flatTiming && !validGameCpuSample)
			RecordMissingFeatureCostSample(sample.gameCpu);
		if (summary.flatTiming)
			sample.pendingFlatSamples.push_back({ summary.flatPresentId + 1, summary.flatTimingEpoch, {} });

		double remainingSampleWeight = std::min(1.0, remainingDurationMs / frameMs);
		while (remainingSampleWeight > 1.0e-9) {
			const auto blockIndex = std::min(
				static_cast<std::size_t>(sample.sampledDurationMs / kFeatureCostIntervalMilliseconds),
				kFeatureCostMeasurementBlockCount - 1);
			const double blockEndMs =
				static_cast<double>(blockIndex + 1) * kFeatureCostIntervalMilliseconds;
			const double remainingBlockMs = blockEndMs - sample.sampledDurationMs;
			const double availableFrameMs = frameMs * remainingSampleWeight;
			const bool completesBlock = availableFrameMs >= remainingBlockMs;
			const double chunkDurationMs = std::min(availableFrameMs, remainingBlockMs);
			const double chunkWeight = chunkDurationMs / frameMs;
			if (chunkWeight <= 0.0)
				break;

			AddFeatureCostMoment(sample.frame, blockIndex, summary.frameSampleMs, chunkWeight);
			if (summary.flatTiming)
				sample.pendingFlatSamples.back().weights[blockIndex] += chunkWeight;
			if (!summary.flatTiming && validGameGpuSample)
				AddFeatureCostMoment(sample.gameGpu, blockIndex, summary.gameGpuSampleMs, chunkWeight);
			if (!summary.flatTiming && validGameCpuSample)
				AddFeatureCostMoment(sample.gameCpu, blockIndex, summary.gameCpuSampleMs, chunkWeight);

			sample.sampledDurationMs = completesBlock ?
			                               blockEndMs :
			                               sample.sampledDurationMs + chunkDurationMs;
			remainingSampleWeight -= chunkWeight;
		}
		sample.sampledDurationMs = std::min(
			kFeatureCostMeasurementMilliseconds,
			sample.sampledDurationMs);

		return sample.sampledDurationMs >= kFeatureCostMeasurementMilliseconds ?
		           FeatureCostSampleResult::Complete :
		           FeatureCostSampleResult::Pending;
	}

	void FinalizeFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		const auto frame = AnalyzeFeatureCostMetric(
			state.currentSample.frame,
			state.testSample.frame);
		state.delta.frame = frame.delta;
		state.delta.fps = AnalyzeFeatureCostFps(frame);
		const auto gameGpu = AnalyzeFeatureCostMetric(
			state.currentSample.gameGpu,
			state.testSample.gameGpu);
		const auto gameCpu = AnalyzeFeatureCostMetric(
			state.currentSample.gameCpu,
			state.testSample.gameCpu);
		state.delta.gameGpu = gameGpu.delta;
		state.delta.gameCpu = gameCpu.delta;
	}

	void PrepareFeatureCostPhase(
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		double currentTime,
		double waitSeconds)
	{
		state.phase = phase;
		state.phaseStartTime = currentTime;
		state.phaseDeadlineTime = currentTime + waitSeconds;
	}

	double GetFeatureCostComparisonWaitSeconds(const Feature* feature)
	{
		if (!feature)
			return kFeatureCostComparisonWaitSeconds;

		const double featureWaitSeconds = feature->GetPerformanceCostMeasurementSettleSeconds(false);
		if (!std::isfinite(featureWaitSeconds))
			return kFeatureCostComparisonWaitSeconds;

		return std::max(kFeatureCostComparisonWaitSeconds, featureWaitSeconds);
	}

	PerformanceTuningController::FeatureCostWaits GetFeatureCostWaits(const Feature* feature, bool batch)
	{
		if (batch && feature)
			return PerformanceTuningController::BatchFeatureWaits(
				feature->GetPerformanceCostMeasurementSettleSeconds(true),
				feature->GetPerformanceCostMeasurementSettleSeconds(false),
				feature->Feature::GetPerformanceCostMeasurementSettleSeconds(true));
		return { kFeatureCostInitialWaitSeconds, GetFeatureCostComparisonWaitSeconds(feature), kFeatureCostRestoreWaitSeconds };
	}

	double GetFeatureCostExpectedRunSeconds(const Feature* feature, bool batch = false)
	{
		const auto waits = GetFeatureCostWaits(feature, batch);
		return waits.TotalSeconds(kFeatureCostMeasurementSeconds);
	}

	json BatchFeatureTimingJson(const Feature* feature)
	{
		const auto waits = GetFeatureCostWaits(feature, true);
		return { { "initialWaitMs", waits.current * 1000.0 }, { "currentMeasurementMs", kFeatureCostMeasurementMilliseconds },
			{ "comparisonWaitMs", waits.comparison * 1000.0 }, { "offMeasurementMs", kFeatureCostMeasurementMilliseconds },
			{ "restoreWaitMs", waits.restoration * 1000.0 }, { "expectedRunMs", waits.TotalSeconds(kFeatureCostMeasurementSeconds) * 1000.0 } };
	}

	double GetFeatureCostRemainingSeconds(
		const FeatureCostMeasurementState& state,
		double currentTime)
	{
		const auto remainingWait = [&]() {
			return std::max(0.0, state.phaseDeadlineTime - currentTime);
		};
		const auto remainingSample = [](const FeatureCostSample& sample) {
			return std::max(
				0.0,
				(kFeatureCostMeasurementMilliseconds - sample.sampledDurationMs) / 1000.0);
		};
		const double comparisonWaitSeconds = state.waits.comparison;
		double remainingSeconds = 0.0;
		switch (state.phase) {
		case FeatureCostMeasurementPhase::AwaitingMenuClose:
			remainingSeconds = state.waits.TotalSeconds(kFeatureCostMeasurementSeconds);
			break;
		case FeatureCostMeasurementPhase::PreparingCurrent:
			remainingSeconds = remainingWait() +
			                   kFeatureCostMeasurementSeconds +
			                   comparisonWaitSeconds +
			                   kFeatureCostMeasurementSeconds +
			                   state.waits.restoration;
			break;
		case FeatureCostMeasurementPhase::MeasuringCurrent:
			remainingSeconds = remainingSample(state.currentSample) +
			                   comparisonWaitSeconds +
			                   kFeatureCostMeasurementSeconds +
			                   state.waits.restoration;
			break;
		case FeatureCostMeasurementPhase::PreparingTest:
			remainingSeconds = remainingWait() +
			                   kFeatureCostMeasurementSeconds +
			                   state.waits.restoration;
			break;
		case FeatureCostMeasurementPhase::MeasuringTest:
			remainingSeconds = remainingSample(state.testSample) +
			                   state.waits.restoration;
			break;
		case FeatureCostMeasurementPhase::Restoring:
			remainingSeconds = remainingWait();
			break;
		case FeatureCostMeasurementPhase::Idle:
		case FeatureCostMeasurementPhase::Complete:
			break;
		}

		// Readiness can extend a phase beyond its minimum deadline. Keep an active
		// countdown visible until the safety timeout or successful restoration.
		return IsFeatureCostMeasurementActive(state) ? std::max(1.0, remainingSeconds) : 0.0;
	}

	void ResetFeatureCostSampleWindow(FeatureCostSample& sample, const ProfilingRenderer::PerformanceTimingSummary& current)
	{
		sample = {};
		// Exclude in-flight compositor frames which started before the measurement boundary.
		sample.lastFrameCount = current.vrTiming ? current.vrLatestFrameIndex : current.frameCount;
		sample.vrTimingEpoch = current.vrTimingEpoch;
		sample.vrWindowInitialized = current.vrTiming && current.vrLatestFrameIndex != 0;
	}

	FeatureCostSampleResult AddFeatureCostSample(
		FeatureCostSample& sample, const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		if (!summary.vrTiming)
			return AddFeatureCostFrame(sample, summary);
		if (!sample.vrWindowInitialized) {
			ResetFeatureCostSampleWindow(sample, summary);
			return FeatureCostSampleResult::Pending;
		}
		if (sample.vrTimingEpoch != summary.vrTimingEpoch)
			return FeatureCostSampleResult::Interrupted;
		for (const auto& source : summary.vrSamples) {
			if (source.frameIndex <= sample.lastFrameCount)
				continue;
			ProfilingRenderer::PerformanceTimingSummary frame;
			frame.frameCount = source.frameIndex;
			frame.valid = source.hasFrame;
			frame.frameSampleMs = source.frameMs;
			frame.hasFrameSample = source.hasFrame;
			frame.gameGpuSampleMs = source.gpuMs;
			frame.hasGameGpuSample = source.hasGpu;
			frame.gameCpuSampleMs = source.cpuMs;
			frame.hasGameCpuSample = source.hasCpu;
			const auto result = AddFeatureCostFrame(sample, frame);
			if (result != FeatureCostSampleResult::Pending)
				return result;
		}
		return FeatureCostSampleResult::Pending;
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
		if (MenuUI::PerformanceResultsPage::GetStore().Busy())
			return "results_busy";
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
		return PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(feature);
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

	bool BeginSavedRun(const char* kind)
	{
		auto& store = MenuUI::PerformanceResultsPage::GetStore();
		if (!store.TryPause())
			return false;
		if (!g_savedRun.is_null())
			return true;
		try {
			static const std::string processor = [] {
				int info[4]{};
				__cpuid(info, static_cast<int>(0x80000000));
				if (static_cast<unsigned>(info[0]) < 0x80000004)
					return std::string("Unavailable");
				char name[49]{};
				for (int i = 0; i < 3; ++i) {
					__cpuid(info, static_cast<int>(0x80000002) + i);
					std::memcpy(name + i * 16, info, 16);
				}
				return std::string(name);
			}();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* location = player ? player->GetCurrentLocation() : nullptr;
			auto* worldspace = player ? player->GetWorldspace() : nullptr;
			json context = { { "build", BuildProvenance::GetProducer() },
				{ "runtime", std::string(magic_enum::enum_name(REL::Module::GetRuntime())) },
				{ "gameVersion", REL::Module::get().version().string() },
				{ "gpu", globals::state->adapterDescription }, { "cpu", processor },
				{ "resolution", { globals::state->screenSize.x, globals::state->screenSize.y } },
				{ "cell", GetMeasurementCellId() }, { "cellName", cell ? cell->GetName() : "" },
				{ "cellEditorId", Util::GetFormEditorID(cell) },
				{ "locationName", location ? location->GetName() : "" },
				{ "worldspaceName", worldspace ? worldspace->GetName() : "" },
				{ "weather", globals::game::sky && globals::game::sky->currentWeather ? json(globals::game::sky->currentWeather->GetFormID()) : json(nullptr) },
				{ "gameHour", globals::game::calendar ? json(globals::game::calendar->GetHour()) : json(nullptr) },
				{ "timingDefinitions", { { "version", Util::VRFrameTiming::kDefinitionVersion },
										   { "frame", globals::game::isVR ? "OpenVR ClientFrameIntervalMs" : "game frame cadence" },
										   { "gpu", globals::game::isVR ? "OpenVR TotalRenderGpuMs" : "D3D11 whole-frame GPU" },
										   { "cpu", globals::game::isVR ? "OpenVR NewFrameReadyMs - NewPosesReadyMs + CompositorRenderCpuMs" : "QPC render-to-present CPU" } } } };
			if (player) {
				const auto position = player->GetPosition(), rotation = player->GetAngle();
				context["playerPosition"] = { position.x, position.y, position.z };
				context["playerRotation"] = { rotation.x, rotation.y, rotation.z };
			}
			context["poseNote"] = "Player position and rotation only; headset pose and moving scene content are not matched automatically.";
			g_savedRun = { { "schema", 1 }, { "id", PerformanceResults::NewId() }, { "started", PerformanceResults::Timestamp() },
				{ "kind", kind }, { "status", "running" }, { "context", std::move(context) },
				{ "settings", CaptureQuickScanSettings() }, { "results", json::array() }, { "failure", nullptr } };
			g_savedRun["settings"]["$renderer"] = { { "shadersEnabled", globals::shaderCache->IsEnabled() },
				{ "shadersRequested", globals::shaderCache->IsEnableRequested() }, { "blockedShader", globals::shaderCache->blockedKeyIndex },
				{ "definesGeneration", globals::state->GetShaderDefinesGeneration() } };
			g_savedRunStart = ImGui::GetTime();
			const auto& theme = globals::menu->GetTheme();
			json reportTheme;
			Menu::PaletteToJson(reportTheme, theme.FullPalette);
			reportTheme["Palette"] = { { "Background", theme.Palette.Background }, { "Text", theme.Palette.Text },
				{ "FrameBorder", theme.Palette.FrameBorder }, { "Separator", theme.Palette.Separator }, { "ResizeGrip", theme.Palette.ResizeGrip } };
			reportTheme["StatusPalette"] = { { "InfoColor", theme.StatusPalette.InfoColor }, { "Warning", theme.StatusPalette.Warning },
				{ "Disable", theme.StatusPalette.Disable }, { "RestartNeeded", theme.StatusPalette.RestartNeeded },
				{ "SuccessColor", theme.StatusPalette.SuccessColor }, { "Error", theme.StatusPalette.Error } };
			g_savedRun["presentation"] = { { "theme", std::move(reportTheme) } };
			g_savedRows.clear();
			return true;
		} catch (const std::exception& error) {
			logger::error("Could not initialize saved performance results: {}", error.what());
			g_savedRun = nullptr;
			store.Resume();
			return false;
		}
	}

	const char* QuickScanPhaseName()
	{
		using enum PerformanceQuickScan::Phase;
		switch (g_quickScan.controller.phase) {
		case AwaitingMenuClose:
			return "awaiting_menu_close";
		case Settling:
			return "settling";
		case BetweenScans:
			return "between_scans";
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

	json QuickScanWholeFrameMetrics()
	{
		const auto& timing = g_quickScan.wholeFrame;
		json metrics = json::object();
		for (const auto& [name, channel] : { std::pair{ "frame", &timing.frame }, { "gpu", &timing.gpu }, { "cpu", &timing.cpu } }) {
			const auto mean = timing.Mean(*channel);
			metrics[name] = { { "unit", "ms" }, { "value", mean ? json(*mean) : json(nullptr) }, { "samples", channel->moments.sampleWeight }, { "missingSamples", channel->missingSamples } };
		}
		const auto frame = timing.Mean(timing.frame);
		metrics["fps"] = { { "unit", "fps" }, { "value", frame && *frame > 0 ? json(1000.0 / *frame) : json(nullptr) },
			{ "samples", timing.frame.moments.sampleWeight }, { "missingSamples", timing.frame.missingSamples } };
		return metrics;
	}

	json QuickScanStatus()
	{
		json rows = json::array();
		for (const auto& row : g_quickScan.rows) {
			rows.push_back({ { "feature", row.feature }, { "label", row.label }, { "coverage", row.coverage },
				{ "gpuMs", row.gpuMs ? json(*row.gpuMs) : json(nullptr) },
				{ "cpuMs", row.cpuMs ? json(*row.cpuMs) : json(nullptr) } });
		}
		return { { "phase", QuickScanPhaseName() }, { "active", g_quickScan.controller.Active() },
			{ "scanIndex", g_quickScan.controller.scanIndex }, { "scanCount", g_quickScan.controller.scanCount },
			{ "gapSeconds", PerformanceQuickScan::kBatchGapSeconds },
			{ "captureRequestedUtc", g_quickScan.captureRequestedUtc.empty() ? json(nullptr) : json(g_quickScan.captureRequestedUtc) },
			{ "captureResolvedUtc", g_quickScan.captureResolvedUtc.empty() ? json(nullptr) : json(g_quickScan.captureResolvedUtc) },
			{ "windowSemantics", "latest_completed_compositor_timestamps_at_request_and_resolution_including_query_latency" },
			{ "startSteamVrSeconds", g_quickScan.startSteamVrSeconds }, { "endSteamVrSeconds", g_quickScan.endSteamVrSeconds },
			{ "captureId", g_quickScan.captureId }, { "requestedFrames", PerformanceQuickScan::kCaptureFrames },
			{ "renderConfiguration", { { "shadersEnabled", g_quickScan.controller.renderConfiguration.shadersEnabled },
										 { "shaderEnableRequested", g_quickScan.controller.renderConfiguration.shadersRequested },
										 { "shaderDefinesGeneration", g_quickScan.controller.renderConfiguration.definesGeneration },
										 { "blockedShaderIndex", g_quickScan.controller.renderConfiguration.blockedShaderIndex } } },
			{ "resolvedFrames", g_quickScan.resolvedFrames }, { "settleSeconds", g_quickScan.controller.scanIndex > 1 ? PerformanceQuickScan::kBatchGapSeconds : PerformanceQuickScan::kSettleSeconds },
			{ "invalidGpuFrames", g_quickScan.invalidGpuFrames }, { "invalidCpuFrames", g_quickScan.invalidCpuFrames },
			{ "maximumSeconds", PerformanceQuickScan::kMaximumSeconds },
			{ "excludedTimings", json::array({ "shared_shader_work", "other_gpu_queues", "uninstrumented_work" }) },
			{ "captureSeconds", g_quickScan.captureSeconds }, { "timingSemantics", "instrumented_self_time_ms_per_frame" },
			{ "wholeFrameMetrics", QuickScanWholeFrameMetrics() },
			{ "isOnOffCost", false }, { "failure", g_quickScan.failure.empty() ? json(nullptr) : json(g_quickScan.failure) },
			{ "results", std::move(rows) } };
	}

	void FinishQuickScan(PerformanceQuickScan::Phase phase, std::string failure = {}, bool reopen = true)
	{
		auto& scan = g_quickScan;
		scan.capture.Release();
		scan.controller.phase = phase;
		scan.failure = std::move(failure);
		if (phase != PerformanceQuickScan::Phase::Complete)
			scan.rows.clear();
		if (!g_savedRun.is_null() && g_savedRun["kind"] == "quick_scan") {
			g_savedRun["capture"] = QuickScanStatus();
			g_savedRun["captures"].push_back(g_savedRun["capture"]);
			auto sampling = g_savedRun["capture"];
			sampling.erase("results");
			if (phase == PerformanceQuickScan::Phase::Complete)
				g_savedRun["results"].push_back({ { "id", fmt::format("{}:$whole_frame", scan.controller.scanIndex) },
					{ "label", "Whole frame (CSX)" }, { "source", "csx_whole_frame" }, { "scanIndex", scan.controller.scanIndex },
					{ "sampling", sampling }, { "coverage", "Performance Tuning whole-frame samples during this capture; FPS = 1000 / mean frame interval." },
					{ "metrics", QuickScanWholeFrameMetrics() } });
			for (const auto& row : scan.rows) {
				g_savedRun["results"].push_back({ { "id", scan.controller.scanCount == 1 ? row.feature : fmt::format("{}:{}", scan.controller.scanIndex, row.feature) },
					{ "label", row.label }, { "scanIndex", scan.controller.scanIndex }, { "sampling", sampling }, { "coverage", row.coverage },
					{ "metrics", { { "gpu", { { "unit", "ms" }, { "value", row.gpuMs ? json(*row.gpuMs) : json(nullptr) } } },
									 { "cpu", { { "unit", "ms" }, { "value", row.cpuMs ? json(*row.cpuMs) : json(nullptr) } } } } } });
			}
			if (phase != PerformanceQuickScan::Phase::Complete)
				g_savedRun["results"].push_back({ { "id", fmt::format("{}:failed", scan.controller.scanIndex) },
					{ "label", "Incomplete scan" }, { "scanIndex", scan.controller.scanIndex }, { "sampling", sampling }, { "failure", scan.failure },
					{ "metrics", { { "gpu", { { "unit", "ms" }, { "value", nullptr } } }, { "cpu", { { "unit", "ms" }, { "value", nullptr } } } } } });
			if (phase == PerformanceQuickScan::Phase::Complete && scan.controller.Advance(ImGui::GetTime())) {
				g_savedRun["elapsedSeconds"] = std::max(0.0, ImGui::GetTime() - g_savedRunStart);
				auto& store = MenuUI::PerformanceResultsPage::GetStore();
				store.Save(g_savedRun);
				store.Resume();
				scan.rows.clear();
				scan.captureId = scan.resolvedFrames = scan.invalidGpuFrames = scan.invalidCpuFrames = 0;
				scan.captureSeconds = 0;
				scan.captureRequestedUtc.clear();
				scan.captureResolvedUtc.clear();
				scan.startSteamVrSeconds = scan.endSteamVrSeconds = 0;
				scan.wholeFrame = {};
				return;
			}
			FinishSavedRun(QuickScanPhaseName(), scan.failure);
		}
		SyncFeatureCostVanityCameraSuppression();
		if (reopen && scan.reopenMenu && globals::menu && !globals::menu->IsEnabled)
			globals::menu->OpenMenu();
	}

	const char* StartQuickScan(bool devBenchOwned, bool batch = false, bool recordFpsVR = false)
	try {
		if (const auto* error = GetFeatureCostStartError(ImGui::GetTime()))
			return error;
		if (PerformanceQuickScan::IsGameMenuOpen(globals::game::ui))
			return PerformanceQuickScan::kGameMenuOpenError;
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
		if (!g_featureCostVanityCameraSuppression.Acquire())
			return "camera_suppression_failed";
		if (!BeginSavedRun("quick_scan")) {
			SyncFeatureCostVanityCameraSuppression();
			return "results_busy";
		}
		RestoreProfilerStateAfterPerformanceTuning();
		pending.controller.Begin(ImGui::GetTime(), pending.reopenMenu, CaptureQuickScanRenderConfiguration(), batch);
		g_savedRun["scanCount"] = pending.controller.scanCount;
		g_savedRun["automaticCsv"] = batch || recordFpsVR;
		g_savedRun["captures"] = json::array();
		g_quickScan = std::move(pending);
		if (recordFpsVR && globals::game::isVR) {
			const auto documents = Util::GetDocumentsPath();
			g_scanFpsVR = std::make_shared<PerformanceFpsVR::Session>(documents ? *documents / L"fpsVR" / L"CSV" : std::filesystem::path{});
			g_savedRun["fpsVR"] = { { "status", "pending" }, { "scans", json::array() } };
		} else if (recordFpsVR) {
			g_savedRun["fpsVR"] = { { "status", "omitted" }, { "reason", "fpsVR requires the VR runtime." }, { "scans", json::array() } };
		}
		if (g_quickScan.reopenMenu)
			globals::menu->CloseMenu();
		return nullptr;
	} catch (const std::exception& error) {
		logger::error("Quick scan could not start: {}", error.what());
		if (g_quickScan.controller.Active())
			FinishQuickScan(PerformanceQuickScan::Phase::Failed, "Quick scan initialization failed.");
		else {
			FinishSavedRun("failed", "Quick scan initialization failed.");
			SyncFeatureCostVanityCameraSuppression();
		}
		return "quick_scan_start_failed";
	}

	std::string QuickScanStartFailureMessage(const char* error, bool batch)
	{
		if (!error)
			return {};
		if (std::string_view(error) == PerformanceQuickScan::kGameMenuOpenError)
			return "Quick scan not started: a game menu is open. Close the game menu, then start the scan again.";
		return fmt::format("{} could not start: {}", batch ? "Scan batch" : "Quick scan", error);
	}

	bool QuickScanReady()
	{
		if (MenuUI::PerformanceResultsPage::GetStore().Busy())
			return false;
		if ((globals::shaderCache && globals::shaderCache->IsCompiling()) ||
			!globals::features::upscaling.IsPerformanceCostMeasurementReady())
			return false;
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->loaded && Util::FeatureProfiling::Find(feature->GetShortName()) &&
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
			FinishQuickScan(Failed, "The scene or measurement ownership changed.");
			return;
		}
		if (!globals::shaderCache) {
			FinishQuickScan(Failed, "Shader settings changed during the quick scan.");
			return;
		}
		if (!globals::profiler || !globals::profiler->IsInitialized()) {
			FinishQuickScan(Failed, "The profiler became unavailable.");
			return;
		}
		auto* profiler = globals::profiler;
		const bool recorderReady = !g_scanFpsVR || g_scanFpsVR->Prepared();
		if (g_scanFpsVR && !scan.fpsVrPrepared && recorderReady) {
			scan.fpsVrPrepared = true;
			scan.controller.started = now;
		}
		const auto action = scan.controller.Poll(now, QuickScanReady(), CaptureQuickScanRenderConfiguration(), recorderReady,
			PerformanceQuickScan::IsGameMenuOpen(globals::game::ui));
		if (action == GameMenuOpened) {
			FinishQuickScan(Failed, "Quick scan stopped because a game menu opened. Close the game menu, then start the scan again.");
			return;
		}
		if (action == ConfigurationChanged) {
			FinishQuickScan(Failed, "Shader settings changed during the quick scan.");
			return;
		}
		if (action == TimedOut) {
			FinishQuickScan(Failed, recorderReady ? "The quick scan timed out." : "fpsVR preparation timed out.");
			return;
		}
		if (action == Interrupted) {
			FinishQuickScan(Failed, "Rendering became unready during capture.");
			return;
		}
		const auto timing = ProfilingRenderer::CaptureFrameTimingSummary(false);
		if (action == StartCapture) {
			if (!MenuUI::PerformanceResultsPage::GetStore().TryPause())
				return;
			if (profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running) {
				FinishQuickScan(Failed, "Another profiler capture is active.");
				return;
			}
			if (CaptureQuickScanSettings() != scan.settings) {
				FinishQuickScan(Failed, "Feature settings changed while settling.");
				return;
			}
			if (!scan.capture.Start(*profiler, PerformanceQuickScan::kCaptureFrames)) {
				FinishQuickScan(Failed, "The profiler could not start the capture.");
				return;
			}
			scan.captureId = scan.capture.SessionId();
			scan.gpuRefusals = profiler->GetSlotRefusals();
			scan.cpuRefusals = profiler->GetCpuSlotRefusals();
			scan.captureStarted = now;
			scan.captureRequestedUtc = PerformanceResults::Timestamp();
			scan.wholeFrame.Begin(timing);
			if (timing.vrTiming) {
				if (!timing.vrSamples.empty()) {
					scan.startSteamVrSeconds = timing.vrSamples.back().systemTime;
					scan.steamVrEpoch = timing.vrTimingEpoch;
				}
			}
			scan.controller.phase = Capturing;
		}
		if (scan.controller.phase != Capturing)
			return;
		scan.wholeFrame.Push(timing);
		const auto progress = profiler->GetBoundedCaptureProgress();
		scan.captureSeconds = now - scan.captureStarted;
		if (progress.sessionId != scan.captureId || progress.state == Profiler::CaptureSessionState::Cancelled ||
			!profiler->IsUserEnabled()) {
			FinishQuickScan(Failed, "The owned profiler capture was interrupted.");
			return;
		}
		scan.resolvedFrames = progress.resolvedFrames;
		scan.invalidGpuFrames = progress.invalidGpuFrames;
		scan.invalidCpuFrames = progress.invalidCpuFrames;
		if (scan.invalidGpuFrames || scan.invalidCpuFrames) {
			FinishQuickScan(Failed, "Profiler queries were missing or invalid; results would be incomplete.");
			return;
		}
		if (profiler->GetSlotRefusals() != scan.gpuRefusals || profiler->GetCpuSlotRefusals() != scan.cpuRefusals) {
			FinishQuickScan(Failed, "Profiler capacity was exceeded; results would be incomplete.");
			return;
		}
		if (progress.state == Profiler::CaptureSessionState::Completed) {
			scan.captureResolvedUtc = PerformanceResults::Timestamp();
			if (timing.vrTiming) {
				if (timing.vrTimingEpoch == scan.steamVrEpoch && !timing.vrSamples.empty())
					scan.endSteamVrSeconds = timing.vrSamples.back().systemTime;
			}
			const auto* timers = profiler->GetBoundedCaptureResults(scan.captureId);
			if (!timers || progress.resolvedFrames != PerformanceQuickScan::kCaptureFrames ||
				CaptureQuickScanSettings() != scan.settings) {
				FinishQuickScan(Failed, "Capture data or feature settings changed.");
				return;
			}
			CollectQuickScanResults(*timers);
			FinishQuickScan(Complete);
		}
	} catch (const std::exception& error) {
		logger::error("Quick scan failed: {}", error.what());
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, "Quick scan data collection failed.");
	}

	bool BeginFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime,
		const json& originalState,
		bool allowClosedMenu,
		bool resetTrace,
		bool devBenchOwned)
	{
		if (GetFeatureCostEnvironmentError() || !feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement() ||
			!feature->IsPerformanceCostMeasurementEnabled() || GetFeatureToggleBlockReason(feature))
			return false;
		if (GetFeatureCostRestartCooldownRemaining(currentTime) > 0.0) {
			logger::warn("Actual feature cost measurement was not started because the {}-second restart cooldown is active", kFeatureCostRestartCooldownSeconds);
			return false;
		}
		if (IsAnyFeatureCostMeasurementActive()) {
			logger::warn("Actual feature cost measurement was not started because another measurement is active");
			return false;
		}
		if (!g_featureCostVanityCameraSuppression.Acquire()) {
			logger::error("Actual feature cost measurement was not started because the automatic vanity camera could not be suppressed");
			return false;
		}
		auto* menu = globals::menu;
		if (!menu || (!menu->IsEnabled && !allowClosedMenu)) {
			g_featureCostVanityCameraSuppression.Release();
			logger::error("Actual feature cost measurement was not started because the CSX menu could not be closed");
			return false;
		}

		if (!BeginSavedRun("feature_comparison")) {
			SyncFeatureCostVanityCameraSuppression();
			return false;
		}
		CaptureProfilerStateForPerformanceTuning();
		if (resetTrace)
			ResetFeatureCostTrace();
		state = {};
		state.waits = GetFeatureCostWaits(feature, g_featureCostBatch.active);
		state.originalState = originalState;
		state.cellFormId = GetMeasurementCellId();
		state.devBenchOwned = devBenchOwned;
		state.reopenMenuOnCompletion = menu->IsEnabled;
		state.runStartTime = currentTime;
		if (menu->IsEnabled) {
			state.phase = FeatureCostMeasurementPhase::AwaitingMenuClose;
			state.phaseStartTime = currentTime;
			menu->CloseMenu();
		} else {
			PrepareFeatureCostPhase(
				FeatureCostMeasurementPhase::PreparingCurrent,
				state,
				currentTime,
				state.waits.current);
		}
		return true;
	}

	void StartFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (!feature)
			return;
		if (GetFeatureCostStartError(currentTime) || !BeginFeatureCostMeasurement(
														 feature, state, currentTime, feature->CapturePerformanceCostMeasurementState(), false, true, false)) {
			state = {};
			state.phase = FeatureCostMeasurementPhase::Complete;
			state.failureMessage = "Measurement could not start. Close the editor and wait for gameplay and feature settings to settle.";
		}
	}

	void ApplyFeatureCostMeasurementTestState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature)
			return;

		feature->SetPerformanceCostMeasurementEnabled(false);
		state.testStateApplied = true;
	}

	void RestoreFeatureCostMeasurementOriginalState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature || !state.testStateApplied)
			return;

		feature->RestorePerformanceCostMeasurementState(state.originalState);
		state.testStateApplied = false;
	}

	void StopFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state, double currentTime, std::string message)
	{
		const bool restoring = state.testStateApplied || state.phase == FeatureCostMeasurementPhase::Restoring;
		RestoreFeatureCostMeasurementOriginalState(feature, state);
		state.delta = {};
		state.failureMessage = std::move(message);
		if (restoring) {
			state.runStartTime = currentTime;
			PrepareFeatureCostPhase(FeatureCostMeasurementPhase::Restoring, state, currentTime, state.waits.restoration);
		} else {
			state.phase = FeatureCostMeasurementPhase::Complete;
			StartFeatureCostRestartCooldown(currentTime);
		}
	}

	void UpdateFeatureBatchBudget(Feature* feature, FeatureCostMeasurementState& state, double currentTime)
	{
		if (!g_featureCostBatch.active)
			return;
		const double elapsed = currentTime - g_featureCostBatch.started;
		if (elapsed >= PerformanceTuningController::kFeatureBatchMaximumSeconds) {
			RestoreFeatureCostMeasurementOriginalState(feature, state);
			state.delta = {};
			if (!state.failureMessage.empty())
				state.failureMessage += " ";
			state.failureMessage += "Full scan reached the five-minute limit. Original settings were requested; this comparison is unavailable.";
			if (!feature->IsPerformanceCostMeasurementReady() || feature->CapturePerformanceCostMeasurementState() != state.originalState)
				state.failureMessage += " Restoration could not be confirmed before the deadline.";
			state.phase = FeatureCostMeasurementPhase::Complete;
			g_featureCostBatch.failureMessage = state.failureMessage;
			StartFeatureCostRestartCooldown(currentTime);
		} else if (state.phase != FeatureCostMeasurementPhase::Restoring &&
				   PerformanceTuningController::BatchNeedsRestoration(elapsed, state.waits.restoration)) {
			g_featureCostBatch.failureMessage = "Full scan reached its time budget. Completed results are retained.";
			StopFeatureCostMeasurement(feature, state, currentTime, g_featureCostBatch.failureMessage);
		}
	}

	void BeginFeatureCostSampleWindow(
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		ResetFeatureCostSampleWindow(sample, current);
		state.phase = phase;
		state.phaseStartTime = currentTime;
	}

	bool RestartInterruptedFeatureCostSample(
		FeatureCostSampleResult result,
		FeatureCostSample& sample,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (result != FeatureCostSampleResult::Interrupted)
			return false;

		ResetFeatureCostSampleWindow(sample, current);
		state.phaseStartTime = currentTime;
		return true;
	}

	void UpdateFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		if (!feature || !IsFeatureCostMeasurementActive(state) ||
			state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose)
			return;
		if (current.flatTiming) {
			ResolveFlatFeatureCostSamples(state.currentSample, current);
			ResolveFlatFeatureCostSamples(state.testSample, current);
		}

		if (state.phase == FeatureCostMeasurementPhase::PreparingCurrent) {
			if (currentTime < state.phaseDeadlineTime || !feature->IsPerformanceCostMeasurementReady())
				return;

			BeginFeatureCostSampleWindow(
				state.currentSample,
				FeatureCostMeasurementPhase::MeasuringCurrent,
				state,
				current,
				currentTime);
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::PreparingTest) {
			if (currentTime < state.phaseDeadlineTime || !feature->IsPerformanceCostMeasurementReady())
				return;

			BeginFeatureCostSampleWindow(
				state.testSample,
				FeatureCostMeasurementPhase::MeasuringTest,
				state,
				current,
				currentTime);
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::Restoring) {
			if (currentTime < state.phaseDeadlineTime || !feature->IsPerformanceCostMeasurementReady())
				return;

			if (current.flatTiming) {
				ResolveFlatFeatureCostSamples(state.currentSample, current, true);
				ResolveFlatFeatureCostSamples(state.testSample, current, true);
			}
			if (feature->CapturePerformanceCostMeasurementState() != state.originalState) {
				state.delta = {};
				if (!state.failureMessage.empty())
					state.failureMessage += " ";
				state.failureMessage += "The original feature settings could not be restored.";
			} else if (state.failureMessage.empty()) {
				FinalizeFeatureCostMeasurement(state);
			}
			state.phase = FeatureCostMeasurementPhase::Complete;
			StartFeatureCostRestartCooldown(currentTime);
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
			if (!feature->IsPerformanceCostMeasurementReady()) {
				state.currentSample = {};
				PrepareFeatureCostPhase(
					FeatureCostMeasurementPhase::PreparingCurrent,
					state,
					currentTime,
					state.waits.current);
				return;
			}
			const auto sampleResult = AddFeatureCostSample(state.currentSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.currentSample,
					current,
					state,
					currentTime)) {
				return;
			}
			if (sampleResult == FeatureCostSampleResult::Complete) {
				ApplyFeatureCostMeasurementTestState(feature, state);
				state.testSample = {};
				PrepareFeatureCostPhase(
					FeatureCostMeasurementPhase::PreparingTest,
					state,
					currentTime,
					state.waits.comparison);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
			if (!feature->IsPerformanceCostMeasurementReady()) {
				state.testSample = {};
				PrepareFeatureCostPhase(
					FeatureCostMeasurementPhase::PreparingTest,
					state,
					currentTime,
					state.waits.comparison);
				return;
			}
			const auto sampleResult = AddFeatureCostSample(state.testSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.testSample,
					current,
					state,
					currentTime)) {
				return;
			}
			if (sampleResult == FeatureCostSampleResult::Complete) {
				RestoreFeatureCostMeasurementOriginalState(feature, state);
				PrepareFeatureCostPhase(
					FeatureCostMeasurementPhase::Restoring,
					state,
					currentTime,
					state.waits.restoration);
			}
		}
	}

	const char* GetQualityModeId(std::uint32_t qualityMode)
	{
		switch (qualityMode) {
		case 1:
			return "hoshipa";
		case 2:
			return "ultra_quality";
		case 3:
			return "quality";
		case 4:
			return "balanced";
		case 5:
			return "performance";
		case 6:
			return "ultra_performance";
		case 0:
		default:
			return "native_aa";
		}
	}

	UpscalingCostSweepCase BuildUpscalingCostSweepCase(
		const json& baseState,
		Upscaling::UpscaleMethod method,
		std::uint32_t qualityMode,
		bool renderScaleMode,
		bool fsr4RuntimeEnabled = false,
		uint32_t dlssPreset = Upscaling::kDLSSPresetK)
	{
		const bool isDLSS = method == Upscaling::UpscaleMethod::kDLSS;
		const bool isFSR = method == Upscaling::UpscaleMethod::kFSR;
		const std::string methodId = method == Upscaling::UpscaleMethod::kTAA ? "taa" :
		                                                                        (isDLSS ? "dlss" : (fsr4RuntimeEnabled ? "fsr4" : "fsr3"));
		const std::string qualityId = method == Upscaling::UpscaleMethod::kTAA ? "native" :
		                                                                         GetQualityModeId(qualityMode);
		const char* qualityName = method == Upscaling::UpscaleMethod::kTAA ?
		                              "Native" :
		                              Upscaling::GetQualityModeName(qualityMode, isDLSS);

		UpscalingCostSweepCase result;
		result.id = fmt::format("{}_{}", methodId, qualityId);
		if (method == Upscaling::UpscaleMethod::kTAA) {
			result.label = "TAA";
		} else if (isDLSS) {
			result.label = fmt::format(
				"DLSS {} (Profile {})",
				qualityName,
				Upscaling::GetDLSSPresetName(dlssPreset));
		} else if (isFSR) {
			result.label = fmt::format("{} {}", FidelityFX::GetFsrSelectionLabel(fsr4RuntimeEnabled), qualityName);
		}

		result.profile = baseState;
		result.profile["upscaleMethod"] = static_cast<std::uint32_t>(method);
		result.profile["upscaleMethodNoDLSS"] = static_cast<std::uint32_t>(Upscaling::UpscaleMethod::kFSR);
		result.profile["qualityMode"] = qualityMode;
		result.profile["dlssPreset"] = isDLSS ?
		                                   dlssPreset :
		                                   baseState.value("dlssPreset", Upscaling::kDLSSPresetK);
		result.profile["renderScaleMode"] = renderScaleMode ? 1u : 0u;
		result.profile["perfMode"] = renderScaleMode ? 1u : 0u;
		result.profile["fsr4RuntimeEnable"] = fsr4RuntimeEnabled;
		return result;
	}

	std::vector<UpscalingCostSweepCase> BuildNvidiaUpscalingCostSweepCases(
		const json& baseState,
		uint32_t dlssPreset)
	{
		std::vector<UpscalingCostSweepCase> cases;
		cases.reserve(15);
		cases.push_back(BuildUpscalingCostSweepCase(
			baseState,
			Upscaling::UpscaleMethod::kTAA,
			0,
			false));
		for (std::uint32_t qualityMode = 0; qualityMode <= Upscaling::kQualityModeMaxIndex; ++qualityMode) {
			cases.push_back(BuildUpscalingCostSweepCase(
				baseState,
				Upscaling::UpscaleMethod::kDLSS,
				qualityMode,
				qualityMode != 0,
				false,
				dlssPreset));
		}
		for (std::uint32_t qualityMode = 0; qualityMode <= Upscaling::kQualityModeMaxIndex; ++qualityMode) {
			cases.push_back(BuildUpscalingCostSweepCase(
				baseState,
				Upscaling::UpscaleMethod::kFSR,
				qualityMode,
				qualityMode != 0));
		}
		return cases;
	}

	std::vector<UpscalingCostSweepCase> BuildAmdUpscalingCostSweepCases(const json& baseState)
	{
		std::vector<UpscalingCostSweepCase> cases;
		cases.reserve(15);
		cases.push_back(BuildUpscalingCostSweepCase(
			baseState,
			Upscaling::UpscaleMethod::kTAA,
			0,
			false));
		for (const bool fsr4RuntimeEnabled : { false, true }) {
			for (std::uint32_t qualityMode = 0; qualityMode <= Upscaling::kQualityModeMaxIndex; ++qualityMode) {
				cases.push_back(BuildUpscalingCostSweepCase(
					baseState,
					Upscaling::UpscaleMethod::kFSR,
					qualityMode,
					qualityMode != 0,
					fsr4RuntimeEnabled));
			}
		}
		return cases;
	}

	bool IsUpscalingCostSweepStateSelected(const json& profile)
	{
		const auto& upscaling = globals::features::upscaling;
		const auto desired = upscaling.GetPendingVRRenderScaleDesiredProfile();
		const auto method = Upscaling::ResolvePerformanceCostMeasurementMethod(
			profile.value("upscaleMethod", 0u),
			profile.value("upscaleMethodNoDLSS", 0u));
		const uint32_t qualityMode = profile.value("qualityMode", 0u);
		const bool renderScaleMode = profile.value("renderScaleMode", 0u) != 0;
		if (desired.method != method ||
			desired.qualityMode != qualityMode ||
			desired.renderScaleModeEnabled != renderScaleMode ||
			desired.perfModeEnabled != renderScaleMode) {
			return false;
		}
		if (method == Upscaling::UpscaleMethod::kDLSS &&
			desired.dlssPreset != profile.value("dlssPreset", Upscaling::kDLSSPresetK)) {
			return false;
		}
		if (method == Upscaling::UpscaleMethod::kFSR &&
			desired.fsr4RuntimeEnabled != profile.value("fsr4RuntimeEnable", false)) {
			return false;
		}

		return upscaling.settings.foveatedVendorDispatch ==
		           profile.value(
					   "foveatedVendorDispatch",
					   upscaling.settings.foveatedVendorDispatch) &&
		       upscaling.settings.periphery_taa_enable ==
		           profile.value(
					   "periphery_taa_enable",
					   upscaling.settings.periphery_taa_enable);
	}

	bool IsUpscalingCostSweepProfileSelected(const UpscalingCostSweepCase& sweepCase)
	{
		return IsUpscalingCostSweepStateSelected(sweepCase.profile);
	}

	bool IsUpscalingCostSweepFsr4ProviderReady(const UpscalingCostSweepCase& sweepCase)
	{
		const auto method = static_cast<Upscaling::UpscaleMethod>(
			sweepCase.profile.value("upscaleMethod", 0u));
		if (method != Upscaling::UpscaleMethod::kFSR ||
			!sweepCase.profile.value("fsr4RuntimeEnable", false)) {
			return true;
		}

		const auto& fidelityFX = Upscaling::fidelityFX;
		return !fidelityFX.IsRuntimeUpscalerFailureLatched() &&
		       !fidelityFX.IsRuntimeFsr4FailureLatched() &&
		       fidelityFX.HasRuntimeUpscalerSupportCheckResult() &&
		       fidelityFX.IsRuntimeUpscalerSupportConfirmed() &&
		       fidelityFX.IsRuntimeUpscalerProviderMatchingRequestedVersion();
	}

	bool IsUpscalingCostSweepNoneSelected()
	{
		const auto& upscaling = globals::features::upscaling;
		const auto desired = upscaling.GetPendingVRRenderScaleDesiredProfile();
		return desired.method == Upscaling::UpscaleMethod::kNONE &&
		       !desired.renderScaleModeEnabled &&
		       !desired.perfModeEnabled &&
		       !upscaling.settings.foveatedVendorDispatch &&
		       !upscaling.settings.periphery_taa_enable;
	}

	bool IsUpscalingCostSweepMeasurementStateExpected(
		const UpscalingCostSweepCase& sweepCase,
		FeatureCostMeasurementPhase phase)
	{
		if (phase == FeatureCostMeasurementPhase::PreparingTest ||
			phase == FeatureCostMeasurementPhase::MeasuringTest) {
			return IsUpscalingCostSweepNoneSelected();
		}

		if (!IsUpscalingCostSweepProfileSelected(sweepCase))
			return false;
		if (phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
			phase == FeatureCostMeasurementPhase::Complete) {
			return IsUpscalingCostSweepFsr4ProviderReady(sweepCase);
		}
		return true;
	}

	void BeginUpscalingCostSweepRestore(
		double currentTime,
		UpscalingCostSweepPhase terminalPhase,
		std::string failureMessage = {})
	{
		auto& sweep = g_upscalingCostSweep;
		sweep.terminalPhase = terminalPhase;
		sweep.failureMessage = std::move(failureMessage);
		globals::features::upscaling.RestorePerformanceCostMeasurementState(sweep.originalState);
		sweep.phase = UpscalingCostSweepPhase::RestoringOriginal;
		sweep.phaseStartTime = currentTime;
	}

	bool StartCurrentUpscalingCostSweepCase(double currentTime)
	{
		auto& sweep = g_upscalingCostSweep;
		if (sweep.currentCaseIndex >= sweep.cases.size())
			return false;

		auto* feature = FindFeatureByShortName("Upscaling");
		if (!feature)
			return false;

		auto& currentCase = sweep.cases[sweep.currentCaseIndex];
		globals::features::upscaling.RestorePerformanceCostMeasurementState(currentCase.profile);
		if (!IsUpscalingCostSweepProfileSelected(currentCase))
			return false;
		auto& measurement = g_costMeasurementStates["Upscaling"];
		if (!BeginFeatureCostMeasurement(
				feature,
				measurement,
				currentTime,
				currentCase.profile,
				true,
				false,
				true)) {
			return false;
		}

		sweep.phase = UpscalingCostSweepPhase::Measuring;
		sweep.phaseStartTime = currentTime;
		return true;
	}

	void FinishUpscalingCostSweepRestore(double currentTime)
	{
		auto& sweep = g_upscalingCostSweep;
		const bool reopenMainMenu = sweep.mainMenuWasOpen;
		const bool reopenEditor = sweep.editorWasOpen;
		sweep.phase = sweep.terminalPhase;
		StartFeatureCostRestartCooldown(currentTime);
		sweep.phaseStartTime = currentTime;
		SyncFeatureCostVanityCameraSuppression();
		RestoreProfilerStateAfterPerformanceTuning();
		if (reopenMainMenu && globals::menu && !globals::menu->IsEnabled)
			globals::menu->OpenMenu();
		if (reopenEditor) {
			auto* editor = EditorWindow::GetSingleton();
			if (editor && !editor->open) {
				editor->open = true;
				editor->UpdateOpenState();
			}
		}
	}

	void UpdateUpscalingCostSweep(double currentTime)
	{
		auto& sweep = g_upscalingCostSweep;
		if (!IsUpscalingCostSweepRunning())
			return;

		auto& upscaling = globals::features::upscaling;
		auto& measurement = g_costMeasurementStates["Upscaling"];
		if (sweep.phase == UpscalingCostSweepPhase::AwaitingMenuClose)
			return;

		if (sweep.phase == UpscalingCostSweepPhase::Measuring) {
			if (measurement.phase == FeatureCostMeasurementPhase::Idle) {
				BeginUpscalingCostSweepRestore(
					currentTime,
					UpscalingCostSweepPhase::Failed,
					"The active Upscaling measurement was lost.");
				return;
			}
			if (sweep.currentCaseIndex >= sweep.cases.size() ||
				!IsUpscalingCostSweepMeasurementStateExpected(
					sweep.cases[sweep.currentCaseIndex],
					measurement.phase)) {
				const bool fsr4ProviderFallback =
					sweep.currentCaseIndex < sweep.cases.size() &&
					IsUpscalingCostSweepProfileSelected(sweep.cases[sweep.currentCaseIndex]) &&
					(measurement.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
						measurement.phase == FeatureCostMeasurementPhase::Complete) &&
					!IsUpscalingCostSweepFsr4ProviderReady(sweep.cases[sweep.currentCaseIndex]);
				// The sweep restores its captured original directly; avoid queuing an
				// intermediate case transition before that authoritative restore.
				measurement = {};
				BeginUpscalingCostSweepRestore(
					currentTime,
					UpscalingCostSweepPhase::Failed,
					fsr4ProviderFallback ?
						fmt::format("The {} provider fell back before its measurement could complete.", FidelityFX::GetFsrSelectionLabel(true)) :
						"The active Upscaling profile changed outside the measurement protocol.");
				return;
			}
			if (measurement.phase != FeatureCostMeasurementPhase::Complete)
				return;

			if (!measurement.failureMessage.empty()) {
				const std::string failure = fmt::format(
					"{}: {}",
					sweep.cases[sweep.currentCaseIndex].id,
					measurement.failureMessage);
				measurement = {};
				BeginUpscalingCostSweepRestore(currentTime, UpscalingCostSweepPhase::Failed, failure);
				return;
			}

			auto result = sweep.cases[sweep.currentCaseIndex];
			result.delta = measurement.delta;
			sweep.results.push_back(std::move(result));
			CaptureSavedFeature("Upscaling", measurement);
			measurement = {};
			++sweep.currentCaseIndex;
			if (sweep.currentCaseIndex >= sweep.cases.size()) {
				BeginUpscalingCostSweepRestore(currentTime, UpscalingCostSweepPhase::Complete);
				return;
			}

			sweep.phase = UpscalingCostSweepPhase::InterCaseCooldown;
			sweep.phaseStartTime = currentTime;
			return;
		}

		if (sweep.phase == UpscalingCostSweepPhase::InterCaseCooldown) {
			if (!MenuUI::PerformanceResultsPage::GetStore().TryPause())
				return;
			if (GetFeatureCostRestartCooldownRemaining(currentTime) > 0.0)
				return;
			if (!StartCurrentUpscalingCostSweepCase(currentTime)) {
				BeginUpscalingCostSweepRestore(
					currentTime,
					UpscalingCostSweepPhase::Failed,
					"The next Upscaling measurement could not start.");
			}
			return;
		}

		if (sweep.phase == UpscalingCostSweepPhase::RestoringOriginal) {
			const double restoreElapsed = currentTime - sweep.phaseStartTime;
			if (restoreElapsed >= kFeatureCostMaximumRunSeconds) {
				sweep.terminalPhase = UpscalingCostSweepPhase::Failed;
				if (sweep.failureMessage.empty())
					sweep.failureMessage = "Timed out while restoring the original Upscaling profile.";
				FinishUpscalingCostSweepRestore(currentTime);
				return;
			}
			if (restoreElapsed < kFeatureCostRestoreWaitSeconds ||
				!upscaling.IsPerformanceCostMeasurementReady() ||
				!IsUpscalingCostSweepStateSelected(sweep.originalState)) {
				return;
			}
			FinishUpscalingCostSweepRestore(currentTime);
		}
	}

	bool CancelUpscalingCostSweep(double currentTime)
	{
		if (!IsUpscalingCostSweepRunning())
			return false;

		auto& measurement = g_costMeasurementStates["Upscaling"];
		if (IsFeatureCostMeasurementActive(measurement) ||
			measurement.phase == FeatureCostMeasurementPhase::Complete) {
			measurement = {};
		}

		if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::RestoringOriginal) {
			g_upscalingCostSweep.terminalPhase = UpscalingCostSweepPhase::Cancelled;
			g_upscalingCostSweep.failureMessage = "Cancelled by DevBench.";
		} else {
			BeginUpscalingCostSweepRestore(
				currentTime,
				UpscalingCostSweepPhase::Cancelled,
				"Cancelled by DevBench.");
		}
		return true;
	}

	void RenderFeatureCostValue(const char* value, const ImVec4& color)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		const SKSE::stl::scope_exit restoreColor([] { ImGui::PopStyleColor(); });
		MenuUI::DetailText(value);
	}

	void RenderFeatureCostPercentage(const FeatureCostMetricDelta& metric)
	{
		if (!metric.available || !metric.hasStandardError || !metric.hasCostPercent) {
			RenderFeatureCostValue("--", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			return;
		}

		const int direction = metric.significant ? GetDirectionFromFeatureCostFrameTimeDelta(static_cast<float>(metric.costPercent)) : 0;
		RenderFeatureCostValue(fmt::format("{:+.1f}%{}", metric.costPercent, metric.significant ? "*" : "").c_str(),
			direction != 0 ? Util::Color::PerformanceDelta(direction) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Frame/CPU/GPU: (current - off) / current. FPS loss: (off - current) / off.");
			ImGui::TextUnformatted("Negative costs indicate a performance saving. * indicates p <= 0.05.");
			ImGui::Text("Missing raw samples: %zu", metric.missingSampleCount);
		}
	}

	void RenderFeatureCostMetricRow(
		const char* label,
		const FeatureCostMetricDelta& metric,
		int direction,
		bool fps)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextColored(Util::Color::SecondaryText(), "%s", label);
		Util::AddTooltip("* marks a statistically significant difference (p <= 0.05). A dagger and number identify one or two missing raw samples; three or more make that timing unavailable (--).");
		ImGui::TableSetColumnIndex(1);
		if (!metric.available || !metric.hasStandardError) {
			RenderFeatureCostValue("--", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TableSetColumnIndex(2);
			RenderFeatureCostValue("--", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			return;
		}

		const int colorDirection = metric.significant ? direction : 0;
		const char* significanceMarker = metric.significant ? "*" : "";
		std::string missingSampleMarker;
		if (metric.missingSampleCount > 0)
			missingSampleMarker = fmt::format(" \xE2\x80\xA0{}", metric.missingSampleCount);
		const auto value = fps ?
		                       fmt::format("{:+.1f}{} \xC2\xB1 {:.1f}{}", metric.value, significanceMarker, metric.standardError, missingSampleMarker) :
		                       fmt::format("{:+.3f}{} \xC2\xB1 {:.3f} ms{}", metric.value, significanceMarker, metric.standardError, missingSampleMarker);
		RenderFeatureCostValue(value.c_str(), colorDirection != 0 ? Util::Color::PerformanceDelta(colorDirection) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
		ImGui::TableSetColumnIndex(2);
		RenderFeatureCostPercentage(metric);
	}

	struct MeasurementTableStyle
	{
		MeasurementTableStyle()
		{
			const float font = ImGui::GetFontSize();
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { font * .75f, font * .5f });
			ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_TableRowBg, { 0, 0, 0, 0 });
			ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Util::Color::WithAlpha(ImGui::GetStyleColorVec4(ImGuiCol_Text), .035f));
		}
		~MeasurementTableStyle()
		{
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar();
		}
		MeasurementTableStyle(const MeasurementTableStyle&) = delete;
		MeasurementTableStyle& operator=(const MeasurementTableStyle&) = delete;
	};

	void RenderMeasurementTableHeaders()
	{
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		for (int column = 0; column < ImGui::TableGetColumnCount(); ++column) {
			ImGui::TableSetColumnIndex(column);
			ImGui::TextWrapped("%s", ImGui::TableGetColumnName(column));
		}
	}

	void RenderMetricCounter(const char* label, float value, const char* format, bool valid,
		float peak = -1.0f, const char* tooltip = nullptr)
	{
		ImGui::PushID(label);
		const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
		const float font = ImGui::GetFontSize();
		auto background = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
		background.w *= MenuUI::SettingsSurfaceOpacityScale;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { font * .4f, font * .6f });
		const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleVar(); ImGui::PopStyleColor(); });
		const float height = font * 5.0f + ImGui::GetStyle().ItemSpacing.y * 2;
		const bool visible = ImGui::BeginChild("##Counter", { 0, height }, ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const SKSE::stl::scope_exit endChild([tooltip] {
			ImGui::EndChild();
			if (tooltip)
				Util::AddTooltip(tooltip);
		});
		if (!visible)
			return;
		ImGui::TextColored(Util::Color::SecondaryText(), "%s", label);
		char text[64];
		std::snprintf(text, sizeof(text), valid ? format : "--", value);
		const float valueY = ImGui::GetCursorPosY();
		const float textWidth = ImGui::GetFont()->CalcTextSizeA(font * 1.6f, FLT_MAX, 0, text).x;
		const float textScale = std::min(1.0f, ImGui::GetContentRegionAvail().x / std::max(1.0f, textWidth));
		{
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * 1.6f * textScale);
			const SKSE::stl::scope_exit restoreFont([] { ImGui::PopFont(); });
			if (valid)
				ImGui::TextUnformatted(text);
			else
				ImGui::TextDisabled("%s", text);
		}
		if (valid && peak >= 0.0f) {
			ImGui::SetCursorPosY(valueY + font * 1.6f + ImGui::GetStyle().ItemSpacing.y);
			std::snprintf(text, sizeof(text), "Peak %.2f ms", peak);
			const float peakScale = std::min(1.0f, ImGui::GetContentRegionAvail().x / std::max(1.0f, ImGui::CalcTextSize(text).x));
			ImGui::PushFont(ImGui::GetFont(), ImGui::GetStyle().FontSizeBase * peakScale);
			const SKSE::stl::scope_exit restoreFont([] { ImGui::PopFont(); });
			ImGui::TextColored(Util::Color::SecondaryText(), "%s", text);
		}
	}

	void RenderTopPerformanceCounters(const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		float displayGpuMs = 0.0f;
		const bool hasDisplayGpu = TryGetDisplayGpuMs(summary, displayGpuMs);
		float displayCpuMs = 0.0f;
		const bool hasDisplayCpu = TryGetDisplayCpuMs(summary, displayCpuMs);

		const char* averagingDetails = summary.vrTiming ?
		                                   "Live average over the last 1 second. Peak is the highest timing in that same window." :
		                                   "Live average over up to 60 recent samples.";
		const auto frameDetails = fmt::format(
			"Application frame interval in milliseconds. CPU and GPU work can overlap; their timings are not added to obtain Frame.\n\n"
			"{}\n-- means no valid timing is available.",
			averagingDetails);
		const auto fpsDetails = fmt::format(
			"Application frames per second: 1000 divided by the average Frame interval in milliseconds. "
			"This is application cadence, not the display refresh rate or a count of generated or reprojected frames.\n\n"
			"{}\n-- means no valid frame timing is available.",
			summary.vrTiming ? "Calculated from the last 1 second of frame timings." : "Calculated from up to 60 recent frame timings.");
		const auto& stats = summary.vrDisplay;
		std::string gpuDetails = summary.vrTiming ? fmt::format(
														"SteamVR total GPU interval, including application and compositor work.\nScene GPU: {}\n"
														"fpsVR displays a peak; compare the peak below with its refresh interval set to 1 s.",
														stats.sceneGpu.count ? fmt::format("{:.2f} ms average / {:.2f} ms peak", stats.sceneGpu.Average(), stats.sceneGpu.peak) : "unavailable") :
		                                            "Whole-frame D3D11 GPU timing. Unavailable with the D3D12 frame-generation path.";
		std::string cpuDetails = summary.vrTiming ? fmt::format(
														"Poses ready to submission, plus compositor CPU time. Excludes pose waiting; does not measure all CPU threads.\n"
														"Pose wait: {}\nfpsVR displays the peak over its refresh interval.",
														stats.poseWait.count ? fmt::format("{:.2f} ms average / {:.2f} ms peak", stats.poseWait.Average(), stats.poseWait.peak) : "unavailable") :
		                                            "Elapsed CPU render-to-present interval. Unavailable with the D3D12 frame-generation path.";
		const auto timingDetails = fmt::format("\n\n{}\n-- means no valid timing is available.", averagingDetails);
		gpuDetails += timingDetails;
		cpuDetails += timingDetails;

		const float font = ImGui::GetFontSize();
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { font * .3f, font * .22f });
		const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleVar(); });
		if (!ImGui::BeginTable("##PerformanceTuningTopCounters", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
			return;
		const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
		ImGui::TableNextColumn();
		RenderMetricCounter("FPS", summary.fps, "%.0f", summary.fps > 0.0f, -1.0f, fpsDetails.c_str());
		ImGui::TableNextColumn();
		RenderMetricCounter("Frame", summary.frameMs, "%.2f ms", summary.frameMs > 0.0f,
			summary.vrTiming && stats.frame.count ? stats.frame.peak : -1.0f,
			frameDetails.c_str());
		ImGui::TableNextColumn();
		RenderMetricCounter("GPU", displayGpuMs, "%.2f ms", hasDisplayGpu,
			summary.vrTiming && stats.gpu.count ? stats.gpu.peak : -1.0f, gpuDetails.c_str());
		ImGui::TableNextColumn();
		RenderMetricCounter("CPU", displayCpuMs, "%.2f ms", hasDisplayCpu,
			summary.vrTiming && stats.cpu.count ? stats.cpu.peak : -1.0f, cpuDetails.c_str());
	}

	void RenderTopPerformanceCounters()
	{
		if (globals::profiler && globals::profiler->IsUserEnabled())
			globals::profiler->RequestCapture();
		RenderTopPerformanceCounters(ProfilingRenderer::CaptureFrameTimingSummary(false));
	}

	bool RenderMeasureButton(bool canStart)
	{
		auto guard = Util::DisableGuard(!canStart);
		return ImGui::Button("Measure", { ImGui::GetFontSize() * 10, ImGui::GetFrameHeight() * 1.4f });
	}

	void RenderMeasurementStatus(bool running)
	{
		const auto results = MenuUI::PerformanceResultsPage::GetStore().Snapshot();
		if (!results.error.empty())
			MenuUI::PageNotice("Results could not be saved. Open Saved results for details and retry.", MenuUI::NoticeKind::Error, results.error);
		else if (results.busy && !running)
			MenuUI::PageNotice("Saving or exporting results...", MenuUI::NoticeKind::Pending);
		const double cooldown = GetFeatureCostRestartCooldownRemaining(ImGui::GetTime());
		if (running)
			MenuUI::DetailText("Running with CS closed");
		else if (PerformanceTuningRenderer::HasActiveMeasurements())
			MenuUI::DetailText("Finish the current measurement first");
		else if (cooldown > 0.0) {
			MenuUI::DetailText(fmt::format("Next measurement in {:.0f}s", std::ceil(cooldown)).c_str());
			Util::AddTooltip("Completed results remain visible. Only the next measurement waits for restored rendering settings to settle.");
		}
	}

	const char* GetFeatureCostComparisonLabel(Feature* feature)
	{
		if (feature && feature->GetShortName() == "Upscaling")
			return "None";

		return "Off";
	}

	std::string GetPerformanceFeatureLabel(Feature* feature)
	{
		const auto name = feature->GetShortName();
		if (name == "VR")
			return "VR optimizations";
		if (name == "LightLimitFix")
			return "Light Limit Fix effects";
		return feature->GetDisplayName();
	}

	const char* GetFeatureCostComparisonDetails(Feature* feature)
	{
		if (!feature)
			return "the feature is switched off.";

		const std::string shortName = feature->GetShortName();
		if (shortName == "Upscaling")
			return "Upscaling is set to None, with foveated upscaling disabled.";
		if (shortName == "NeuralRendering")
			return "Neural Rendering is switched off, retaining its route, character, and colour settings.";
		if (shortName == "VR")
			return "depth culling, screen-space stereo sync, screen-space FOV, stereo blend, shader FOV, and dynamic cubemap throttle are switched off.";
		if (shortName == "AdaptiveBrightness")
			return "all Visuals Lighting, Bloom, Water appearance, profile/location, and wind contributions are bypassed together.";
		if (shortName == "LinearLighting")
			return "Linear Lighting color-space conversions and per-geometry updates are switched off.";
		if (shortName == "ScreenSpaceShadows")
			return "Screen Space Shadows are switched off.";
		if (shortName == "ScreenSpaceGI")
			return "SSGI/AO is switched off.";
		if (shortName == "LightLimitFix")
			return "particle lights, point-light contact shadows, and particle contact shadows are switched off.";
		if (shortName == "Skylighting")
			return "Skylighting's in-game Enable toggle is switched off, so probe updates stop and ambient shading plus reflection occlusion fall back to the unoccluded path.";
		if (shortName == "CloudShadows")
			return "cloud-shadow cubemap updates and projection are switched off.";
		if (shortName == "TerrainBlending")
			return "Terrain Blending is switched off.";
		if (shortName == "TerrainShadows")
			return "Terrain Shadows are switched off.";
		if (shortName == "VolumetricLighting")
			return "Volumetric Lighting is switched off for the current interior/exterior context.";
		if (shortName == "VolumetricShadows")
			return "directional shadow-map copying, downsampling, and blurring are switched off.";
		if (shortName == "Wetterness")
			return "Wetterness is switched off.";
		if (shortName == "Skin")
			return "skin lighting, detail normals and actor wetness are switched off. Male/female Subsurface Scattering controls retain their current settings; no shader compilation is requested.";
		if (shortName == "SubsurfaceScattering")
			return "Subsurface Scattering is switched off.";
		if (shortName == "TruePBR")
			return "True PBR material shading is switched off.";
		if (shortName == "ExtendedMaterials")
			return "complex materials, parallax, legacy terrain parallax, height blending, parallax shadows, and curvature correction are switched off.";
		if (shortName == "DetailMaps")
			return "detail texture lookups, detail shader selection and per-draw resource updates are bypassed; surfaces use their original lighting shaders.";
		if (shortName == "FoliageLighting")
			return "all Foliage Lighting contributions to tree foliage and grass are switched off.";
		if (shortName == "GrassLighting")
			return "the Grass Lighting runtime toggle is switched off, so grass uses the basic pixel-shading path; the installed shader permutation and vertex work remain the same in both windows.";
		if (shortName == "GrassOptimizations")
			return "grass uses native drawing, with optimized batching, density, mesh LOD and grass Hi-Z switched off. Other grass features keep their current settings.";
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

		const double currentTime = ImGui::GetTime();
		const char* blockReason = GetFeatureToggleBlockReason(feature);
		const bool canStartMeasurement =
			feature->loaded && feature->SupportsPerformanceCostMeasurement() &&
			feature->IsPerformanceCostMeasurementEnabled() && feature->IsPerformanceCostMeasurementReady() && !blockReason &&
			GetFeatureCostStartError(currentTime) == nullptr;
		MenuUI::SectionHeading("Measurement");
		if (RenderMeasureButton(canStartMeasurement))
			StartFeatureCostMeasurement(feature, state, currentTime);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (blockReason)
				ImGui::TextUnformatted(blockReason);
			else if (!feature->SupportsPerformanceCostMeasurement())
				ImGui::TextUnformatted("This feature cannot be switched off in game for a cost comparison.");
			else if (!feature->IsPerformanceToggleEnabled())
				ImGui::TextUnformatted("Enable this feature before measuring its cost.");
			else if (!feature->IsPerformanceCostMeasurementEnabled())
				ImGui::TextUnformatted("This feature is enabled but inactive in the current scene.");
			else if (!feature->IsPerformanceCostMeasurementReady())
				ImGui::TextUnformatted(feature->GetPerformanceCostMeasurementWaitText());
			ImGui::TextWrapped(
				"The menu closes for the run and reopens with results. Keep the scene and headset still for about %.0f seconds; the overlay shows progress.",
				GetFeatureCostExpectedRunSeconds(feature));
			ImGui::TextWrapped(
				"The run settles for %.0f seconds, then measures current settings in %zu intervals of %.0f ms. It switches to %s, settles for %.0f seconds and measures %zu intervals. It restores the exact prior state and verifies it after %.0f second.",
				kFeatureCostInitialWaitSeconds,
				kFeatureCostMeasurementBlockCount,
				kFeatureCostIntervalMilliseconds,
				GetFeatureCostComparisonLabel(feature),
				GetFeatureCostComparisonWaitSeconds(feature),
				kFeatureCostMeasurementBlockCount,
				kFeatureCostRestoreWaitSeconds);
			ImGui::TextWrapped(
				"Interrupted game timing restarts only that %.0f-second measurement. Readiness may extend settling/restoration waits.",
				kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped(
				"After completion, wait %.0f seconds before remeasuring or starting the next feature or Upscaling case.",
				kFeatureCostRestartCooldownSeconds);
			ImGui::TextWrapped("GPU/CPU rows allow two missing raw samples across both states. Three or more invalidate only that row; missing GPU/CPU data does not block Frame/FPS.");
			ImGui::TextWrapped("The idle/vanity camera is suppressed throughout the run; its previous delay returns afterward.");
			if (feature && feature->GetShortName() == "Skylighting") {
				ImGui::TextWrapped("For Skylighting, the comparison state is its in-game Enable toggle set to Off, not a lower preset.");
			}
			ImGui::TextWrapped(
				"Comparison: %s - %s",
				GetFeatureCostComparisonLabel(feature),
				GetFeatureCostComparisonDetails(feature));
		}
		RenderMeasurementStatus(IsFeatureCostMeasurementActive(state));
		if (IsFeatureCostMeasurementActive(state))
			return;

		MenuUI::SectionHeading("Results");
		if (state.phase != FeatureCostMeasurementPhase::Complete) {
			MenuUI::DetailText("No measurement yet");
			Util::AddTooltip("Choose Measure to compare this feature in the current scene. Your settings are restored after the run.");
			return;
		}

		if (!state.failureMessage.empty()) {
			MenuUI::PageNotice(state.failureMessage, MenuUI::NoticeKind::Error);
			return;
		}

		if (!state.delta.frame.available && !state.delta.fps.available &&
			!state.delta.gameGpu.available && !state.delta.gameCpu.available) {
			MenuUI::DetailText("No game timing data");
			return;
		}

		ImGui::Spacing();
		const std::string differenceHeader = fmt::format(
			"Mean: Current - {} \xC2\xB1 SE",
			GetFeatureCostComparisonLabel(feature));
		if (ImGui::BeginTable(
				"##FeatureCostResults",
				3,
				ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersInnerH |
					ImGuiTableFlags_PadOuterX |
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, std::ceil(ImGui::CalcTextSize(globals::game::isVR ? "VR GPU total" : "Metric").x));
			ImGui::TableSetupColumn(differenceHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Cost / FPS loss (%)", ImGuiTableColumnFlags_WidthStretch);
			RenderMeasurementTableHeaders();
			RenderFeatureCostMetricRow(
				"Frame",
				state.delta.frame,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.frame.value),
				false);
			RenderFeatureCostMetricRow(
				"FPS",
				state.delta.fps,
				GetDirectionFromFeatureCostFpsDelta(state.delta.fps.value),
				true);
			RenderFeatureCostMetricRow(
				globals::game::isVR ? "VR GPU total" : "GPU",
				state.delta.gameGpu,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameGpu.value),
				false);
			RenderFeatureCostMetricRow(
				globals::game::isVR ? "VR CPU" : "CPU",
				state.delta.gameCpu,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameCpu.value),
				false);
			ImGui::EndTable();
		}
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

	std::vector<Feature*> BuildPerformanceFeatureList()
	{
		std::vector<Feature*> features;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature || !feature->loaded || feature->IsHiddenFromUserView() ||
				!feature->IsInMenu() || !feature->SupportsPerformanceCostMeasurement())
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
		if (shortName == "VR") {
			return {
				"VR",
				"ScreenSpaceShadows",
				"ScreenSpaceGI",
				"DynamicCubemaps",
				"DeferredComposite"
			};
		}

		return { shortName };
	}

	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!IsFeatureCostMeasurementActive(state)) {
			return;
		}

		if (feature && state.testStateApplied)
			RestoreFeatureCostMeasurementOriginalState(feature, state);

		state = {};
	}

	bool CancelOwnedFeatureMeasurements(bool devBenchOwned)
	{
		const bool ownsBatch = g_featureCostBatch.active && g_featureCostBatch.devBenchOwned == devBenchOwned;
		const bool ownsMeasurement = std::ranges::any_of(g_costMeasurementStates, [devBenchOwned](const auto& entry) {
			return entry.second.devBenchOwned == devBenchOwned && IsFeatureCostMeasurementActive(entry.second);
		});
		if (!ownsBatch && !ownsMeasurement)
			return false;

		const double currentTime = ImGui::GetTime();
		bool cancelled = false;
		bool reopenMenu = !devBenchOwned;
		if (g_featureCostBatch.active && g_featureCostBatch.devBenchOwned == devBenchOwned) {
			reopenMenu = reopenMenu || g_featureCostBatch.reopenMenuOnCompletion;
			g_featureCostBatch.active = false;
			g_featureCostBatch.failureMessage = "Cancelled. Completed results are retained.";
			cancelled = true;
		}
		for (auto& [shortName, state] : g_costMeasurementStates) {
			if (state.devBenchOwned != devBenchOwned || !IsFeatureCostMeasurementActive(state))
				continue;
			reopenMenu = reopenMenu || state.reopenMenuOnCompletion;
			state.reopenMenuOnCompletion = reopenMenu;
			StopFeatureCostMeasurement(FindFeatureByShortName(shortName), state, currentTime, "Measurement cancelled.");
			CaptureSavedFeature(shortName, state);
			cancelled = true;
		}
		if (!cancelled)
			return false;
		StartFeatureCostRestartCooldown(currentTime);
		SyncFeatureCostVanityCameraSuppression();
		if (!PerformanceTuningRenderer::HasActiveMeasurements()) {
			RestoreProfilerStateAfterPerformanceTuning();
			if (reopenMenu && globals::menu && !globals::menu->IsEnabled)
				globals::menu->OpenMenu();
		}
		return true;
	}

	void ClearFinishedFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		if (state.phase == FeatureCostMeasurementPhase::Complete) {
			state = {};
		}
	}

	void InvalidateFeatureCostResults()
	{
		if (g_quickScan.controller.phase == PerformanceQuickScan::Phase::Complete)
			g_quickScan = {};
		for (auto& [_, state] : g_costMeasurementStates)
			ClearFinishedFeatureCostMeasurement(state);
		if (!g_featureCostBatch.active)
			g_featureCostBatch = {};
		if (!IsUpscalingCostSweepRunning())
			g_upscalingCostSweep = {};
	}

	bool SetRuntimeFeatureEnabled(Feature* feature, bool enabled)
	{
		g_featureToggleFailure.clear();
		failedToggleFeature = nullptr;
		const auto reject = [feature](const char* reason) {
			failedToggleFeature = feature;
			g_featureToggleFailure = reason;
			return false;
		};
		if (!feature || !feature->loaded || !feature->SupportsPerformanceProfileToggle())
			return reject("This feature is unavailable for runtime toggling.");
		if (PerformanceTuningRenderer::HasActiveMeasurements())
			return reject("Finish the active measurement before changing feature settings.");
		if (Util::IsRuntimeToggleBlocked(globals::state))
			return reject("Load a game and finish loading or saving before changing feature settings.");
		if (const auto* reason = GetFeatureToggleBlockReason(feature))
			return reject(reason);
		if (feature->IsPerformanceToggleEnabled() == enabled)
			return true;
		if (!globals::menu)
			return reject("The settings menu is unavailable.");
		if (!globals::menu->PrepareSettingsMutation())
			return reject("Could not preserve saved settings.");
		const auto shortName = feature->GetShortName();
		auto& saved = g_disabledFeatureConfigurations[shortName];
		bool applied = false;
		try {
			applied = PerformanceTuningController::SetEnabled(*feature, enabled, saved);
		} catch (const std::exception& error) {
			logger::warn("Feature toggle failed for {}: {}", shortName, error.what());
			g_featureToggleFailure = error.what();
		} catch (...) {
			logger::warn("Feature toggle failed unexpectedly for {}", shortName);
			g_featureToggleFailure = "The feature toggle failed unexpectedly. Check CSX.log.";
		}
		if (!saved)
			g_disabledFeatureConfigurations.erase(shortName);
		InvalidateFeatureCostResults();
		globals::menu->RequestSettingsDirtyCheck();
		if (!applied)
			failedToggleFeature = feature;
		return applied;
	}

	bool StartNextFeatureCostBatchMeasurement(double currentTime)
	{
		auto& batch = g_featureCostBatch;
		if (GetFeatureCostEnvironmentError() || GetMeasurementCellId() != batch.cellFormId || batch.nextFeatureIndex >= batch.features.size())
			return false;
		auto* feature = FindFeatureByShortName(batch.features[batch.nextFeatureIndex]);
		if (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement() ||
			!feature->IsPerformanceCostMeasurementEnabled())
			return false;
		if (currentTime - batch.started + GetFeatureCostExpectedRunSeconds(feature, true) + PerformanceTuningController::kFeatureBatchRestorationMarginSeconds > PerformanceTuningController::kFeatureBatchMaximumSeconds) {
			batch.failureMessage = "Full scan cannot fit another comparison inside five minutes. Completed results are retained.";
			return false;
		}
		auto& state = g_costMeasurementStates[feature->GetShortName()];
		if (!BeginFeatureCostMeasurement(feature, state, currentTime,
				feature->CapturePerformanceCostMeasurementState(), true, false, batch.devBenchOwned))
			return false;
		state.reopenMenuOnCompletion = false;
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
		json timingPlan = json::array();
		double expectedSeconds = 0.0;
		for (const auto& name : enabledFeatures) {
			auto timing = BatchFeatureTimingJson(FindFeatureByShortName(name));
			expectedSeconds += timing.at("expectedRunMs").get<double>() / 1000.0;
			timing["feature"] = name;
			timingPlan.push_back(std::move(timing));
		}
		if (expectedSeconds + PerformanceTuningController::kFeatureBatchRestorationMarginSeconds > PerformanceTuningController::kFeatureBatchMaximumSeconds)
			return "feature_batch_exceeds_five_minutes";

		if (!BeginSavedRun("feature_batch"))
			return "results_busy";
		g_savedRun["requestedFeatures"] = enabledFeatures;
		g_savedRun["timingPlan"] = std::move(timingPlan);
		g_savedRun["maximumSeconds"] = PerformanceTuningController::kFeatureBatchMaximumSeconds;
		InvalidateFeatureCostResults();
		ResetFeatureCostTrace();
		g_featureCostBatch.active = true;
		g_featureCostBatch.started = currentTime;
		g_featureCostBatch.expectedSeconds = expectedSeconds;
		g_featureCostBatch.devBenchOwned = devBenchOwned;
		g_featureCostBatch.reopenMenuOnCompletion = globals::menu->IsEnabled;
		g_featureCostBatch.features = std::move(enabledFeatures);
		g_featureCostBatch.cellFormId = GetMeasurementCellId();
		if (!StartNextFeatureCostBatchMeasurement(currentTime)) {
			g_featureCostBatch.active = false;
			RestoreProfilerStateAfterPerformanceTuning();
			g_featureCostBatch.failureMessage = "The feature comparison could not start.";
			FinishSavedRun("failed", g_featureCostBatch.failureMessage);
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
		if (!IsAnyFeatureCostMeasurementActive() && currentTime - batch.started >= PerformanceTuningController::kFeatureBatchMaximumSeconds) {
			batch.failureMessage = "Full scan reached the five-minute limit. Completed results are retained.";
			batch.active = false;
			return batch.reopenMenuOnCompletion;
		}
		if (!IsAnyFeatureCostMeasurementActive() &&
			(GetFeatureCostEnvironmentError() || GetMeasurementCellId() != batch.cellFormId)) {
			batch.failureMessage = "Feature comparisons stopped because the gameplay scene changed. Completed results are retained.";
			batch.active = false;
			return batch.reopenMenuOnCompletion;
		}

		const auto previous = batch.nextFeatureIndex > 0 && batch.nextFeatureIndex <= batch.features.size() ?
		                          g_costMeasurementStates.find(batch.features[batch.nextFeatureIndex - 1]) :
		                          g_costMeasurementStates.end();
		const bool previousComplete = previous != g_costMeasurementStates.end() && previous->second.phase == FeatureCostMeasurementPhase::Complete;
		const bool previousFailed = previous != g_costMeasurementStates.end() && !previous->second.failureMessage.empty();
		switch (PerformanceTuningController::NextBatchAction(batch.nextFeatureIndex, batch.features.size(),
			IsAnyFeatureCostMeasurementActive(), previousComplete, previousFailed, GetFeatureCostRestartCooldownRemaining(currentTime))) {
		case PerformanceTuningController::BatchAction::Wait:
			break;
		case PerformanceTuningController::BatchAction::Finish:
			batch.active = false;
			break;
		case PerformanceTuningController::BatchAction::StartNext:
			if (!MenuUI::PerformanceResultsPage::GetStore().TryPause())
				break;
			if (StartNextFeatureCostBatchMeasurement(currentTime))
				break;
			[[fallthrough]];
		case PerformanceTuningController::BatchAction::Fail:
			if (batch.failureMessage.empty())
				batch.failureMessage = "Feature comparisons stopped because a measurement could not complete. Completed results are retained.";
			batch.active = false;
			break;
		}
		return !batch.active && batch.reopenMenuOnCompletion;
	}

	json FeatureCostMetricDeltaJson(const FeatureCostMetricDelta& metric, std::string_view unit)
	{
		json result = {
			{ "available", metric.available && metric.hasStandardError },
			{ "unit", unit },
			{ "missingSampleCount", metric.missingSampleCount },
		};
		if (metric.available && metric.hasStandardError) {
			result["delta"] = metric.value;
			result["standardError"] = metric.standardError;
			result["pValue"] = metric.pValue;
			result["significant"] = metric.significant;
			result["current"] = metric.currentValue;
			result["comparison"] = metric.comparisonValue;
			result["costPercent"] = metric.hasCostPercent ? json(metric.costPercent) : json(nullptr);
			result["costPercentBasis"] = unit == "fps" ? "fps_loss_relative_to_off" : "share_of_current";
		}
		return result;
	}

	json FeatureCostResultJson(
		std::string_view shortName,
		const FeatureCostMeasurementState& state)
	{
		auto* feature = FindFeatureByShortName(shortName);
		return {
			{ "feature", shortName },
			{ "timingDefinitionVersion", Util::VRFrameTiming::kDefinitionVersion },
			{ "displayName", feature ? json(feature->GetDisplayName()) : json(nullptr) },
			{ "owner", state.devBenchOwned ? "devbench_feature_cost" : "ui" },
			{ "relativeTo", feature ? GetFeatureCostComparisonLabel(feature) : "Off" },
			{ "failure", state.failureMessage.empty() ? json(nullptr) : json(state.failureMessage) },
			{ "game", FeatureCostMetricDeltaJson(state.delta.frame, "ms") },
			{ "fps", FeatureCostMetricDeltaJson(state.delta.fps, "fps") },
			{ "gpu", FeatureCostMetricDeltaJson(state.delta.gameGpu, "ms") },
			{ "cpu", FeatureCostMetricDeltaJson(state.delta.gameCpu, "ms") },
		};
	}

	json UpscalingCostSweepProfileJson(const UpscalingCostSweepCase& sweepCase)
	{
		const std::uint32_t method = sweepCase.profile.value("upscaleMethod", 0u);
		const std::uint32_t qualityMode = sweepCase.profile.value("qualityMode", 0u);
		const bool renderScaleMode = sweepCase.profile.value("renderScaleMode", 0u) != 0;
		const bool isDLSS = method == static_cast<std::uint32_t>(Upscaling::UpscaleMethod::kDLSS);
		const bool isFSR = method == static_cast<std::uint32_t>(Upscaling::UpscaleMethod::kFSR);
		const bool fsr4RuntimeEnabled = sweepCase.profile.value("fsr4RuntimeEnable", false);
		const uint32_t dlssPreset = sweepCase.profile.value("dlssPreset", Upscaling::kDLSSPresetK);
		return {
			{ "id", sweepCase.id },
			{ "label", sweepCase.label },
			{ "method", method },
			{ "qualityMode", qualityMode },
			{ "qualityName", method == static_cast<std::uint32_t>(Upscaling::UpscaleMethod::kTAA) ?
								 "Native" :
								 Upscaling::GetQualityModeName(
									 qualityMode,
									 isDLSS) },
			{ "renderScaleMode", renderScaleMode },
			{ "renderScale", renderScaleMode ? Upscaling::GetQualityModeResolutionScale(qualityMode) : 1.0f },
			{ "dlssPreset", isDLSS ? json(dlssPreset) : json(nullptr) },
			{ "dlssPresetName", isDLSS ? json(Upscaling::GetDLSSPresetName(dlssPreset)) : json(nullptr) },
			{ "fsrRuntime", isFSR ? json(fsr4RuntimeEnabled ? "FSR runtime" : "FSR3") : json(nullptr) },
		};
	}

	json UpscalingCostSweepResultJson(const UpscalingCostSweepCase& result)
	{
		return {
			{ "profile", UpscalingCostSweepProfileJson(result) },
			{ "relativeTo", "none" },
			{ "timingDefinitionVersion", Util::VRFrameTiming::kDefinitionVersion },
			{ "game", FeatureCostMetricDeltaJson(result.delta.frame, "ms") },
			{ "fps", FeatureCostMetricDeltaJson(result.delta.fps, "fps") },
			{ "gpu", FeatureCostMetricDeltaJson(result.delta.gameGpu, "ms") },
			{ "cpu", FeatureCostMetricDeltaJson(result.delta.gameCpu, "ms") },
		};
	}

	void FinishSavedRun(std::string status, std::string failure)
	{
		if (g_savedRun.is_null())
			return;
		g_savedRun["status"] = std::move(status);
		g_savedRun["failure"] = failure.empty() ? json(nullptr) : json(failure);
		g_savedRun["finished"] = PerformanceResults::Timestamp();
		g_savedRun["elapsedSeconds"] = std::max(0.0, ImGui::GetTime() - g_savedRunStart);
		auto& store = MenuUI::PerformanceResultsPage::GetStore();
		if (g_scanFpsVR && g_savedRun["kind"] == "quick_scan") {
			auto session = std::exchange(g_scanFpsVR, {});
			session->RequestFinish();
			store.Save(std::move(g_savedRun), [session = std::move(session)](json& record) { session->Finish(record); });
		} else
			store.Save(std::move(g_savedRun));
		g_savedRun = nullptr;
		store.Resume();
	}

	json SavedSample(const FeatureCostSample& sample)
	{
		json result = { { "sampledDurationMs", sample.sampledDurationMs } };
		for (const auto& [name, metric] : { std::pair{ "frame", &sample.frame }, { "gpu", &sample.gameGpu }, { "cpu", &sample.gameCpu } }) {
			json blocks = json::array();
			for (const auto& block : metric->blocks) blocks.push_back({ { "sum", block.sum }, { "sampleWeight", block.sampleWeight } });
			result[name] = { { "blocks", std::move(blocks) }, { "missingSampleCount", metric->missingSampleCount }, { "accumulationValid", metric->accumulationValid } };
		}
		return result;
	}

	void CaptureSavedFeature(std::string_view name, const FeatureCostMeasurementState& state)
	{
		if (g_savedRun.is_null() || state.phase != FeatureCostMeasurementPhase::Complete || g_savedRun["kind"] == "quick_scan")
			return;
		const bool sweep = g_savedRun["kind"] == "upscaling_sweep";
		std::string id(name), label(name);
		if (sweep) {
			if (g_upscalingCostSweep.currentCaseIndex >= g_upscalingCostSweep.cases.size())
				return;
			id = g_upscalingCostSweep.cases[g_upscalingCostSweep.currentCaseIndex].id;
			label = g_upscalingCostSweep.cases[g_upscalingCostSweep.currentCaseIndex].label;
		} else if (auto* feature = FindFeatureByShortName(name))
			label = feature->GetDisplayName();
		if (!g_savedRows.insert(id).second)
			return;
		const auto source = FeatureCostResultJson(name, state);
		json row = { { "id", id }, { "label", label }, { "relativeTo", source["relativeTo"] }, { "failure", source["failure"] },
			{ "currentSamples", SavedSample(state.currentSample) }, { "comparisonSamples", SavedSample(state.testSample) },
			{ "settings", state.originalState }, { "metrics", json::object() } };
		for (const auto& [key, target] : { std::pair{ "game", "frame" }, { "gpu", "gpu" }, { "cpu", "cpu" }, { "fps", "fps" } }) {
			auto metric = source.at(key);
			metric["value"] = state.failureMessage.empty() ? metric.value("delta", json(nullptr)) : json(nullptr);
			row["metrics"][target] = std::move(metric);
		}
		g_savedRun["results"].push_back(std::move(row));
		if (g_savedRun["kind"] == "feature_batch" || sweep) {
			g_savedRun["elapsedSeconds"] = std::max(0.0, ImGui::GetTime() - g_savedRunStart);
			auto& store = MenuUI::PerformanceResultsPage::GetStore();
			store.Save(g_savedRun);
			store.Resume();
		}
	}

	void UpdateSavedRun()
	{
		if (g_savedRun.is_null() || PerformanceTuningRenderer::HasActiveMeasurements())
			return;
		std::string failure;
		const auto kind = g_savedRun.at("kind").get<std::string>();
		if (kind == "feature_batch")
			failure = g_featureCostBatch.failureMessage;
		else if (kind == "upscaling_sweep")
			failure = g_upscalingCostSweep.failureMessage;
		else
			for (const auto& row : g_savedRun.at("results")) {
				if (row.contains("failure") && row["failure"].is_string()) {
					failure = row["failure"].get<std::string>();
					break;
				}
			}
		if (failure.empty() && g_savedRun.at("results").empty())
			failure = "The measurement ended before producing results.";
		const bool cancelled = failure.find("cancel") != std::string::npos || failure.find("Cancel") != std::string::npos;
		FinishSavedRun(cancelled ? "cancelled" : failure.empty() ? "complete" :
																   "failed",
			failure);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void AppendTimingSource(json& timing, const FeatureCostTraceSample& sample)
	{
		if (sample.vrTiming) {
			timing["timingDefinitionVersion"] = Util::VRFrameTiming::kDefinitionVersion;
			timing["compositorFrameIndex"] = sample.compositorSystemTime > 0.0 ? json(sample.sampleFrameCount) : json(nullptr);
			timing["compositorSystemTimeSeconds"] = sample.compositorSystemTime > 0.0 ? json(sample.compositorSystemTime) : json(nullptr);
			timing["timingEpoch"] = sample.vrTimingEpoch;
			timing["sceneGpuMs"] = sample.hasSceneGpu ? json(sample.sceneGpuMs) : json(nullptr);
			timing["poseWaitMs"] = sample.hasPoseWait ? json(sample.poseWaitMs) : json(nullptr);
		}
		if (!sample.flatTiming)
			return;
		timing["gpuCpuFrameCount"] = sample.samplePresentId != 0 ? json(sample.sampleFrameCount) : json(nullptr);
		timing["gpuCpuPresentId"] = sample.samplePresentId != 0 ? json(sample.samplePresentId) : json(nullptr);
	}

	json FeatureCostTraceJson(std::uint64_t afterSequence, std::size_t maximumSamples)
	{
		maximumSamples = std::clamp<std::size_t>(maximumSamples, 1, kFeatureCostMaximumTracePageSize);
		const std::uint64_t retainedFirstSequence =
			g_featureCostTrace.empty() ? 0 : g_featureCostTrace.front().sequence;
		const bool cursorPrecedesRetainedTrace =
			afterSequence != 0 &&
			retainedFirstSequence > 0 &&
			afterSequence < retainedFirstSequence - 1;
		json samples = json::array();
		std::uint64_t nextAfterSequence = afterSequence;
		bool hasMore = false;
		for (const auto& sample : g_featureCostTrace) {
			if (sample.sequence <= afterSequence)
				continue;
			if (samples.size() >= maximumSamples) {
				hasMore = true;
				break;
			}

			samples.push_back({
				{ "sequence", sample.sequence },
				{ "runElapsedMs", sample.runElapsedMs },
				{ "phaseElapsedMs", sample.phaseElapsedMs },
				{ "phase", sample.phase },
				{ "caseId", sample.caseId },
				{ "frameCount", sample.frameCount },
				{ "frameMs", sample.hasFrame ? json(sample.frameMs) : json(nullptr) },
				{ "gameGpuMs", sample.hasGameGpu ? json(sample.gameGpuMs) : json(nullptr) },
				{ "gameCpuMs", sample.hasGameCpu ? json(sample.gameCpuMs) : json(nullptr) },
			});
			AppendTimingSource(samples.back(), sample);
			nextAfterSequence = sample.sequence;
		}

		return {
			{ "sampleIntervalMs", kFeatureCostTraceIntervalSeconds * 1000.0 },
			{ "requestedAfterSequence", afterSequence },
			{ "cursorPrecedesRetainedTrace", cursorPrecedesRetainedTrace },
			{ "retainedFirstSequence", retainedFirstSequence },
			{ "retainedLastSequence", g_featureCostTrace.empty() ? 0 : g_featureCostTrace.back().sequence },
			{ "nextAfterSequence", nextAfterSequence },
			{ "hasMore", hasMore },
			{ "samples", std::move(samples) },
		};
	}

#endif
	json FeatureCostMeasurementStatusJson(
		std::string_view shortName,
		const FeatureCostMeasurementState& state,
		double currentTime)
	{
		return {
			{ "feature", shortName },
			{ "phase", GetFeatureCostPhaseName(state.phase) },
			{ "phaseElapsedMs", std::max(0.0, currentTime - state.phaseStartTime) * 1000.0 },
			{ "runElapsedMs", std::max(0.0, currentTime - state.runStartTime) * 1000.0 },
			{ "initialWaitMs", state.waits.current * 1000.0 },
			{ "comparisonWaitMs", state.waits.comparison * 1000.0 },
			{ "restoreWaitMs", state.waits.restoration * 1000.0 },
			{ "expectedRunMs", state.waits.TotalSeconds(kFeatureCostMeasurementSeconds) * 1000.0 },
			{ "estimatedRemainingMs", GetFeatureCostRemainingSeconds(state, currentTime) * 1000.0 },
		};
	}

	json BuildMeasurementStatus([[maybe_unused]] std::uint64_t traceAfterSequence, [[maybe_unused]] std::size_t maximumTraceSamples)
	{
		const double currentTime = ImGui::GetTime();
		const FeatureCostMeasurementState* activeMeasurement = nullptr;
		std::string activeFeature;
		for (const auto& [shortName, state] : g_costMeasurementStates) {
			if (!IsFeatureCostMeasurementActive(state))
				continue;
			activeMeasurement = &state;
			activeFeature = shortName;
			break;
		}

		json results = json::array();
		for (const auto& result : g_upscalingCostSweep.results)
			results.push_back(UpscalingCostSweepResultJson(result));

		json featureResults = json::array();
		for (const auto& [shortName, state] : g_costMeasurementStates) {
			if (state.phase == FeatureCostMeasurementPhase::Complete)
				featureResults.push_back(FeatureCostResultJson(shortName, state));
		}

		json availableFeatureCosts = json::array();
		for (auto* feature : BuildPerformanceFeatureList()) {
			if (!feature || !feature->SupportsPerformanceCostMeasurement())
				continue;
			availableFeatureCosts.push_back({
				{ "feature", feature->GetShortName() },
				{ "displayName", feature->GetDisplayName() },
				{ "enabled", feature->IsPerformanceToggleEnabled() },
				{ "measurementEnabled", feature->IsPerformanceCostMeasurementEnabled() },
				{ "comparisonDetails", GetFeatureCostComparisonDetails(feature) },
				{ "comparisonWaitMs", GetFeatureCostComparisonWaitSeconds(feature) * 1000.0 },
				{ "expectedRunMs", GetFeatureCostExpectedRunSeconds(feature) * 1000.0 },
				{ "batchTiming", BatchFeatureTimingJson(feature) },
				{ "ready", feature->IsPerformanceCostMeasurementReady() },
				{ "toggleBlockReason", GetFeatureToggleBlockReason(feature) ? json(GetFeatureToggleBlockReason(feature)) : json(nullptr) },
			});
		}

		json currentCase = nullptr;
		if (g_upscalingCostSweep.currentCaseIndex < g_upscalingCostSweep.cases.size()) {
			currentCase = UpscalingCostSweepProfileJson(
				g_upscalingCostSweep.cases[g_upscalingCostSweep.currentCaseIndex]);
			currentCase["index"] = g_upscalingCostSweep.currentCaseIndex;
		}

		json measurement = activeMeasurement ?
		                       FeatureCostMeasurementStatusJson(activeFeature, *activeMeasurement, currentTime) :
		                       json(nullptr);

#ifdef DEVBENCH_BRIDGE_ENABLED
		json latestTiming = nullptr;
		if (!g_featureCostTrace.empty()) {
			const auto& sample = g_featureCostTrace.back();
			latestTiming = {
				{ "sequence", sample.sequence },
				{ "phase", sample.phase },
				{ "caseId", sample.caseId },
				{ "runElapsedMs", sample.runElapsedMs },
				{ "phaseElapsedMs", sample.phaseElapsedMs },
				{ "frameCount", sample.frameCount },
				{ "frameMs", sample.hasFrame ? json(sample.frameMs) : json(nullptr) },
				{ "gameGpuMs", sample.hasGameGpu ? json(sample.gameGpuMs) : json(nullptr) },
				{ "gameCpuMs", sample.hasGameCpu ? json(sample.gameCpuMs) : json(nullptr) },
			};
			AppendTimingSource(latestTiming, sample);
		}

#endif
		const bool sweepKnown = g_upscalingCostSweep.phase != UpscalingCostSweepPhase::Idle;
		const bool sweepRunning = IsUpscalingCostSweepRunning();
		const bool nvidiaSweep = sweepKnown &&
		                         g_upscalingCostSweep.matrix == UpscalingCostSweepMatrix::Nvidia;
		const auto readiness = CaptureUpscalingCostSweepReadiness(currentTime);
		const auto& fidelityFX = Upscaling::fidelityFX;
		json response = json::object();
		response["metricDefinitions"] = {
			{ "version", Util::VRFrameTiming::kDefinitionVersion },
			{ "runtime", globals::game::isVR ? "vr" : "flat" },
			{ "frame", globals::game::isVR ? "OpenVR ClientFrameIntervalMs" : "game frame cadence" },
			{ "gpu", globals::game::isVR ? "OpenVR TotalRenderGpuMs" : "D3D11 whole-frame GPU" },
			{ "cpu", globals::game::isVR ? "OpenVR NewFrameReadyMs - NewPosesReadyMs + CompositorRenderCpuMs" : "QPC render-to-present CPU" },
			{ "displayWindowMs", globals::game::isVR ? json(Util::VRFrameTiming::kDisplayWindowSeconds * 1000.0) : json(nullptr) },
			{ "displayStatistic", globals::game::isVR ? "mean and peak" : "mean of 60 samples" },
			{ "comparisonStatistic", "mean difference with standard error" },
			{ "completionLagFrames", globals::game::isVR ? Util::VRFrameTiming::kCompletionLagFrames : 0 },
		};
		response["active"] = PerformanceTuningRenderer::HasActiveMeasurements();
		response["quickScan"] = QuickScanStatus();
		response["featureBatch"] = {
			{ "active", g_featureCostBatch.active },
			{ "maximumMs", PerformanceTuningController::kFeatureBatchMaximumSeconds * 1000.0 },
			{ "expectedRunMs", g_featureCostBatch.expectedSeconds * 1000.0 },
			{ "postRunCooldownMs", PerformanceTuningController::kFeatureBatchCooldownSeconds * 1000.0 },
			{ "features", g_featureCostBatch.features },
			{ "startedCount", g_featureCostBatch.nextFeatureIndex },
			{ "failure", g_featureCostBatch.failureMessage.empty() ? json(nullptr) : json(g_featureCostBatch.failureMessage) },
		};
		response["owner"] = g_quickScan.controller.Active() ? (g_quickScan.devBenchOwned ? "devbench_quick_scan" : "ui_quick_scan") : g_featureCostBatch.active ? (g_featureCostBatch.devBenchOwned ? "devbench_feature_batch" : "ui_feature_batch") :
		                                                                                                                          sweepRunning                  ? "devbench_upscaling_sweep" :
		                                                                                                                                                          (activeMeasurement ? (activeMeasurement->devBenchOwned ? "devbench_feature_cost" : "ui") : "none");
		response["sweepPhase"] = GetUpscalingCostSweepPhaseName(g_upscalingCostSweep.phase);
		response["measurement"] = std::move(measurement);
		response["currentCase"] = std::move(currentCase);
		response["currentCaseIndex"] = g_upscalingCostSweep.currentCaseIndex;
		response["caseCount"] = g_upscalingCostSweep.cases.size();
		response["resultCount"] = g_upscalingCostSweep.results.size();
		response["baseline"] = "none";
		response["matrix"] = sweepKnown ? json(GetUpscalingCostSweepMatrixName(g_upscalingCostSweep.matrix)) : json(nullptr);
		response["dlssPreset"] = nvidiaSweep ? json(Upscaling::GetDLSSPresetName(g_upscalingCostSweep.dlssPreset)) : json(nullptr);
		response["readiness"] = {
			{ "ready", readiness.Ready() },
			{ "idle", readiness.idle },
			{ "vr", readiness.vr },
			{ "inGame", readiness.inGame },
			{ "menuAvailable", readiness.menuAvailable },
			{ "measurementSupported", readiness.measurementSupported },
			{ "restartCooldownComplete", readiness.restartCooldownComplete },
		};
		response["capabilities"] = {
			{ "amdAdapter", fidelityFX.IsAmdAdapterDetected() },
			{ "nvidiaAdapter", fidelityFX.IsNvidiaAdapterDetected() },
			{ "dlssCheckComplete", Upscaling::streamline.featureCheckComplete.load(std::memory_order_relaxed) },
			{ "dlssAvailable", Upscaling::streamline.featureDLSS.load(std::memory_order_relaxed) },
			{ "fsr4Available", fidelityFX.IsRuntimeFsr4Available() },
			{ "fsr4SweepAvailable", IsFsr4UpscalingCostSweepAvailable() },
			{ "fsrRuntimeFailureLatched", fidelityFX.IsRuntimeUpscalerFailureLatched() },
			{ "fsr4FailureLatched", fidelityFX.IsRuntimeFsr4FailureLatched() },
			{ "fsrSupportCheckComplete", fidelityFX.HasRuntimeUpscalerSupportCheckResult() },
			{ "fsrSupportConfirmed", fidelityFX.IsRuntimeUpscalerSupportConfirmed() },
		};
		response["failure"] = g_upscalingCostSweep.failureMessage.empty() ? json(nullptr) : json(g_upscalingCostSweep.failureMessage);
		response["restartCooldownRemainingMs"] = GetFeatureCostRestartCooldownRemaining(currentTime) * 1000.0;
		response["timing"] = {
			{ "initialCooldownMs", kFeatureCostInitialWaitSeconds * 1000.0 },
			{ "measurementWindowMs", kFeatureCostMeasurementMilliseconds },
			{ "measurementIntervalMs", kFeatureCostIntervalMilliseconds },
			{ "measurementBlockCount", kFeatureCostMeasurementBlockCount },
			{ "comparisonWaitMs", kFeatureCostComparisonWaitSeconds * 1000.0 },
			{ "upscalingComparisonWaitMs", GetFeatureCostComparisonWaitSeconds(&globals::features::upscaling) * 1000.0 },
			{ "restoreWaitMs", kFeatureCostRestoreWaitSeconds * 1000.0 },
			{ "postRunCooldownMs", kFeatureCostRestartCooldownSeconds * 1000.0 },
		};
#ifdef DEVBENCH_BRIDGE_ENABLED
		response["latestTiming"] = std::move(latestTiming);
		response["trace"] = FeatureCostTraceJson(traceAfterSequence, maximumTraceSamples);
#endif
		response["results"] = std::move(results);
		response["featureResults"] = std::move(featureResults);
		response["availableFeatureCosts"] = std::move(availableFeatureCosts);
		return response;
	}

}

bool PerformanceTuningRenderer::SetFeatureEnabled(Feature* a_feature, bool a_enabled)
{
	return SetRuntimeFeatureEnabled(a_feature, a_enabled);
}

const char* PerformanceTuningRenderer::GetFeatureControlBlockReason(Feature* a_feature)
{
	return GetFeatureToggleBlockReason(a_feature);
}

const char* PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(Feature* a_feature)
{
	if (!a_feature)
		return "Feature is unavailable.";
	auto* sceneSettings = SceneSettingsManager::GetSingleton();
	const auto name = a_feature->GetShortName();
	if (sceneSettings->HasActiveSettingsForFeature(name) && !sceneSettings->IsFeaturePaused(name))
		return "Pause this feature's scene-specific settings before changing or measuring it.";
	return nullptr;
}

bool PerformanceTuningRenderer::ApplyFeatureSettingsTransaction(const std::vector<Feature*>& a_features, const std::function<bool()>& a_apply, bool a_replaceToggleHistory)
{
	const bool applied = PerformanceTuningController::ApplyTransaction(a_features, g_disabledFeatureConfigurations, a_apply, a_replaceToggleHistory);
	NotifyFeatureSettingsChanged(&globals::features::upscaling);
	return applied;
}

json PerformanceTuningRenderer::CaptureFeatureToggleHistory()
{
	auto result = json::object();
	for (const auto& [name, state] : g_disabledFeatureConfigurations)
		result[name] = state ? json{ { "enabled", state->enabledState }, { "disabled", state->disabledState } } : json(nullptr);
	return result;
}

void PerformanceTuningRenderer::RestoreFeatureToggleHistory(const json& a_history)
{
	g_disabledFeatureConfigurations.clear();
	for (const auto& [name, state] : a_history.items())
		g_disabledFeatureConfigurations[name] = state.is_null() ? std::nullopt : std::optional(PerformanceTuningController::DisabledConfiguration{ state.at("enabled"), state.at("disabled") });
}

const char* PerformanceTuningRenderer::GetFeatureToggleFailureReason(Feature* a_feature)
{
	return a_feature && failedToggleFeature == a_feature ?
	           (g_featureToggleFailure.empty() ? "This feature could not change state. Check its settings and runtime requirements." : g_featureToggleFailure.c_str()) :
	           nullptr;
}

void PerformanceTuningRenderer::RenderFeatureEnabledControl(Feature* a_feature, const char* a_tooltip)
{
	const bool supported = a_feature->SupportsPerformanceCostMeasurement();
	bool enabled = supported ? a_feature->IsPerformanceToggleEnabled() : a_feature->loaded;
	const auto* reason = GetFeatureToggleBlockReason(a_feature);
	const bool busy = HasActiveMeasurements();
	{
		auto guard = Util::DisableGuard(!a_feature->loaded || !supported || reason || busy);
		if (Util::Widgets::Checkbox("Enabled", &enabled)) {
			const bool applied = SetRuntimeFeatureEnabled(a_feature, enabled);
			g_featureCostUiMessage = applied ? "" : GetFeatureToggleFailureReason(a_feature);
		}
	}
	const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
	if (!hovered)
		return;
	const char* tooltip = reason ? reason : busy           ? "Wait for the current measurement to finish." :
	                                    !a_feature->loaded ? "Enable this feature in Settings > Startup, save settings and restart." :
	                                    !supported         ? "Change startup in Settings > Startup, save settings and restart to enable or disable this feature." :
	                                                         a_tooltip;
	std::string featureHelp;
	if (!tooltip) {
		featureHelp = a_feature->GetFeatureSummary().first;
		if (const auto newline = featureHelp.find_first_of("\r\n"); newline != std::string::npos)
			featureHelp.resize(newline);
		if (featureHelp.empty())
			featureHelp = "Applies " + a_feature->GetDisplayName() + " to the scene.";
		featureHelp += " Turn off to stop these effects while keeping your settings.";
		tooltip = featureHelp.c_str();
	}
	Util::AddTooltip(tooltip);
}

namespace
{
	void RenderQuickScan()
	{
		RenderTopPerformanceCounters();
		RenderMeasurementStatus(g_quickScan.controller.Active());
		MenuUI::SectionHeading("Quick scan", "Instrumented CPU/GPU self times, not on/off savings. Existing on/off comparisons are unchanged. Keep the scene still. The menu closes; CSX waits three seconds, then captures 300 frames for all instrumented features.");
		{
			auto disabled = Util::DisableGuard(PerformanceTuningRenderer::HasActiveMeasurements() || MenuUI::PerformanceResultsPage::GetStore().Busy());
			static bool recordFpsVR = false;
			if (globals::game::isVR) {
				ImGui::Checkbox("Include fpsVR when running", &recordFpsVR);
				Util::AddTooltip("Uses the running fpsVR installation's recording command. A confirmed active recording is stopped first, then a fresh recording starts for this run and stops after the complete scan batch; fpsVR stays open. If recording cannot be verified, scans continue without fpsVR. Matched whole-frame timings and a combined HTML report are saved automatically; the fpsVR CSV is unchanged.");
			}
			if (ImGui::Button("Start quick scan")) {
				const char* error = StartQuickScan(false, false, recordFpsVR);
				g_featureCostUiMessage = QuickScanStartFailureMessage(error, false);
			}
			Util::AddTooltip("Keep the scene and headset still. The menu closes, waits three seconds and captures 300 frames for all instrumented features, then reopens with results.");
			ImGui::SameLine();
			if (ImGui::Button("3 scans + automatic CSV")) {
				const char* error = StartQuickScan(false, true, recordFpsVR);
				g_featureCostUiMessage = QuickScanStartFailureMessage(error, true);
			}
			Util::AddTooltip("Capture three scans with three-second gaps. Readiness settling occurs during each gap; delays extend the gap. CSV and JSON are saved beside the saved results, including commit, Build ID and capture times. Keep the scene and headset still throughout.");
			const float resultsWidth = ImGui::CalcTextSize("Open results").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float resultsOffset = std::max(0.0f, ImGui::GetContentRegionAvail().x - resultsWidth);
			const float resultsLeft = ImGui::GetCursorPosX() + resultsOffset;
			if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x <= ImGui::GetCursorScreenPos().x + resultsOffset)
				ImGui::SameLine(resultsLeft);
			else
				ImGui::SetCursorPosX(resultsLeft);
			if (ImGui::Button("Open results")) {
				PerformanceTuningRenderer::SelectView("saved_results");
				ImGui::SetScrollY(0);
				return;
			}
			Util::AddTooltip("View saved runs and reports, export them, or open the results folder.");
		}
		if (!g_featureCostUiMessage.empty())
			MenuUI::PageNotice(g_featureCostUiMessage, MenuUI::NoticeKind::Error);
		if (!g_quickScan.failure.empty())
			MenuUI::PageNotice(g_quickScan.failure, MenuUI::NoticeKind::Error);
		if (g_quickScan.controller.scanCount > 1 && !g_quickScan.controller.Active()) {
			const auto saved = MenuUI::PerformanceResultsPage::GetStore().Snapshot();
			MenuUI::PageNotice(saved.busy ? "Saving batch CSV and JSON..." : saved.error.empty() ? "Batch results saved. Open Saved results to view or locate the CSV." :
																								   saved.error,
				saved.error.empty() ? MenuUI::NoticeKind::Information : MenuUI::NoticeKind::Error);
		}
		if (g_quickScan.controller.phase != PerformanceQuickScan::Phase::Complete)
			return;
		ImGui::Text("Captured %u frames in %.1f seconds after settling.", g_quickScan.resolvedFrames, g_quickScan.captureSeconds);
		MenuUI::SectionHeading("Results", "Sorted by GPU time. -- = shared-only, inactive or incomplete coverage. Excludes shared shader work and other GPU queues.");
		if (ImGui::BeginTable("##QuickScanResults", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
			const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn("Feature");
			ImGui::TableSetupColumn("GPU (ms)");
			ImGui::TableSetupColumn("CPU (ms)");
			ImGui::TableSetupColumn("Coverage");
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
}

void PerformanceTuningRenderer::SelectView(std::string_view a_section)
{
	if (!HasActiveMeasurements()) {
		if (a_section == "quick_scan")
			tuningView = 1;
		else if (a_section == "compare" || a_section == "performance")
			tuningView = 0;
		else if (a_section == "saved_results")
			tuningView = 2;
	}
}

void PerformanceTuningRenderer::Render()
{
	FeatureListRenderer::RenderPerformancePage("PerformanceTuning", [] {
		MenuUI::PerformancePage::DrawSaveChoice();
		const char* modes[] = { "Compare", "Quick scan all features", "Saved results" };
		{
			const bool busy = HasActiveMeasurements();
			const auto disabled = Util::DisableGuard(busy);
			if (!busy)
				MenuUI::SettingsSearch::SelectNavigationChoice("Tuning mode", tuningView, modes, 3);
			const float width = ImGui::GetContentRegionAvail().x;
			ImGui::RadioButton(modes[0], &tuningView, 0);
			const float nextWidth = ImGui::CalcTextSize(modes[1]).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
			if (ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= width)
				ImGui::SameLine();
			ImGui::RadioButton(modes[1], &tuningView, 1);
			const float savedWidth = ImGui::CalcTextSize(modes[2]).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
			if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + savedWidth <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
				ImGui::SameLine();
			ImGui::RadioButton(modes[2], &tuningView, 2);
		}
		switch (tuningView) {
		case 0:
			RenderMeasurementSuite();
			break;
		case 1:
			RenderQuickScan();
			break;
		case 2:
			MenuUI::PerformanceResultsPage::Draw();
			break;
		}
	});
}

namespace
{
	void RenderFeatureSetMeasurement(const std::vector<Feature*>& features)
	{
		const bool anyEnabled = std::ranges::any_of(features, [](Feature* feature) {
			return feature->IsPerformanceCostMeasurementEnabled() && !GetFeatureToggleBlockReason(feature);
		});
		MenuUI::SectionHeading("Measurement");
		if (RenderMeasureButton(GetFeatureCostStartError(ImGui::GetTime()) == nullptr && anyEnabled)) {
			const char* error = StartFeatureCostBatch(false);
			g_featureCostUiMessage = error ? std::string_view(error) == "feature_batch_exceeds_five_minutes" ? "The selected features cannot fit inside five minutes. Disable some features and retry." :
			                                                                                                   "Measurement could not start. Check that a game is loaded, the editor is closed, and the features are ready." :
			                                 "";
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("CSX compares each active, editable feature with Off/None and restores exact settings after each comparison. Ordinary features take about 7.5 seconds; temporal features keep their own settling requirements. There is no extra gap after restoration. The full scan has a five-minute budget; delays preserve completed results and mark unfinished comparisons unavailable. Keep the scene still. Cancel with the menu shortcut.");
		RenderMeasurementStatus(g_featureCostBatch.active);
		if (!g_featureCostUiMessage.empty())
			MenuUI::PageNotice(g_featureCostUiMessage, MenuUI::NoticeKind::Error);
		if (!g_featureCostBatch.failureMessage.empty())
			MenuUI::PageNotice(g_featureCostBatch.failureMessage, MenuUI::NoticeKind::Error);
		MenuUI::SectionHeading("Features and results", "Select enabled features below. Comparisons use means with standard errors. Individual on/off costs do not add up to a total. -- = unmeasured or unavailable.");
		const bool busy = PerformanceTuningRenderer::HasActiveMeasurements();
		if (ImGui::BeginTable("##PerformanceFeatureCosts", 5,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Feature (in game toggle)", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Frame (%)");
			ImGui::TableSetupColumn(globals::game::isVR ? "VR CPU (%)" : "CPU (%)");
			ImGui::TableSetupColumn(globals::game::isVR ? "VR GPU total (%)" : "GPU (%)");
			ImGui::TableSetupColumn("FPS loss (%)");
			RenderMeasurementTableHeaders();
			for (auto* feature : features) {
				ImGui::PushID(feature->GetShortName().c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				bool enabled = feature->IsPerformanceToggleEnabled();
				const char* blockReason = GetFeatureToggleBlockReason(feature);
				ImGui::BeginDisabled(busy || blockReason != nullptr);
				if (Util::Widgets::Checkbox("##Enabled", &enabled)) {
					const bool applied = SetRuntimeFeatureEnabled(feature, enabled);
					g_featureCostUiMessage = applied ? "" : PerformanceTuningRenderer::GetFeatureToggleFailureReason(feature);
				}
				ImGui::EndDisabled();
				if (blockReason) {
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(blockReason);
				}
				ImGui::SameLine();
				ImGui::TextWrapped("%s", GetPerformanceFeatureLabel(feature).c_str());
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextWrapped("Compared with %s: %s", GetFeatureCostComparisonLabel(feature), GetFeatureCostComparisonDetails(feature));
				if (blockReason) {
					ImGui::TextDisabled("(controlled)");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextUnformatted(blockReason);
				} else if (enabled && !feature->IsPerformanceCostMeasurementEnabled()) {
					ImGui::TextDisabled("(inactive)");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextWrapped("This feature remains enabled. Its cost comparison requires a scene in which it is active.");
				}
				const auto result = g_costMeasurementStates.find(feature->GetShortName());
				const FeatureCostDelta* delta = result != g_costMeasurementStates.end() &&
				                                        result->second.phase == FeatureCostMeasurementPhase::Complete &&
				                                        result->second.failureMessage.empty() ?
				                                    &result->second.delta :
				                                    nullptr;
				for (const auto* metric : { delta ? &delta->frame : nullptr, delta ? &delta->gameCpu : nullptr,
						 delta ? &delta->gameGpu : nullptr, delta ? &delta->fps : nullptr }) {
					ImGui::TableNextColumn();
					if (metric)
						RenderFeatureCostPercentage(*metric);
					else
						RenderFeatureCostValue("--", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				}
				if (result != g_costMeasurementStates.end() && !result->second.failureMessage.empty()) {
					ImGui::TableSetColumnIndex(0);
					ImGui::TextWrapped("%s", result->second.failureMessage.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (features.empty())
			ImGui::TextDisabled("No loaded features support switching on and off in game.");
	}

}

void PerformanceTuningRenderer::NotifyOverviewInactive()
{
	if (!HasActiveMeasurements())
		RestoreProfilerStateAfterPerformanceTuning();
}

void PerformanceTuningRenderer::RenderMeasurementSuite(Feature* a_feature)
{
	if (a_feature && !a_feature->SupportsPerformanceCostMeasurement())
		return;
	const auto features = a_feature ? std::vector<Feature*>{ a_feature } : BuildPerformanceFeatureList();
	RenderTopPerformanceCounters();
	if (!a_feature) {
		MeasurementTableStyle tableStyle;
		RenderFeatureSetMeasurement(features);
		return;
	}

	const auto name = a_feature->GetShortName();
	ImGui::PushID(name.c_str());
	const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
	const bool profiling = ProfilingRenderer::CanProfileFeature(name);
	MenuUI::DetailGrid suite("##MeasurementSuite", profiling ? 2 : 1);
	MeasurementTableStyle tableStyle;
	suite.Next();
	RenderFeatureCostMeasurement(a_feature, g_costMeasurementStates[name]);
	if (profiling) {
		suite.Next();
		ProfilingRenderer::RenderFeaturePerformanceSummary(name);
	}
}

void PerformanceTuningRenderer::NotifyFeatureSettingsChanged(Feature* a_feature)
{
	if (g_quickScan.controller.Active())
		FinishQuickScan(PerformanceQuickScan::Phase::Failed, "Feature settings changed during the quick scan.");
	if (a_feature)
		g_quickScan.rows.clear();
	if (!a_feature || HasActiveMeasurements())
		return;
	InvalidateFeatureCostResults();
}

void PerformanceTuningRenderer::UpdateClosedMenuMeasurement()
{
	const double currentTime = ImGui::GetTime();
	UpdateQuickScan(currentTime);
	bool shouldReopenMenu = false;
	bool completedDevBenchMeasurement = false;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (!IsFeatureCostMeasurementActive(state))
			continue;

		const bool reopenMenuOnCompletion = state.reopenMenuOnCompletion;
		const bool devBenchOwned = state.devBenchOwned;
		auto* feature = FindFeatureByShortName(shortName);
		if (!feature) {
			logger::error("Actual feature cost measurement stopped because feature '{}' is no longer available", shortName);
			shouldReopenMenu = shouldReopenMenu || reopenMenuOnCompletion;
			completedDevBenchMeasurement = completedDevBenchMeasurement || devBenchOwned;
			state.phase = FeatureCostMeasurementPhase::Complete;
			state.failureMessage = "The feature became unavailable.";
			CaptureSavedFeature(shortName, state);
			continue;
		}

		UpdateFeatureBatchBudget(feature, state, currentTime);
		if (IsFeatureCostMeasurementActive(state) && currentTime - state.runStartTime >= kFeatureCostMaximumRunSeconds) {
			if (state.phase == FeatureCostMeasurementPhase::Restoring && !state.failureMessage.empty()) {
				state.failureMessage += " Restoring the runtime state did not finish before the timeout.";
				state.phase = FeatureCostMeasurementPhase::Complete;
				StartFeatureCostRestartCooldown(currentTime);
			} else {
				StopFeatureCostMeasurement(feature, state, currentTime, "Measurement stopped: timing did not complete.");
			}
		}
		if (state.phase != FeatureCostMeasurementPhase::Restoring && IsFeatureCostMeasurementActive(state)) {
			const bool expectsEnabled = state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose ||
			                            state.phase == FeatureCostMeasurementPhase::PreparingCurrent || state.phase == FeatureCostMeasurementPhase::MeasuringCurrent;
			if (GetFeatureCostEnvironmentError() || GetMeasurementCellId() != state.cellFormId || !feature->loaded || GetFeatureToggleBlockReason(feature))
				StopFeatureCostMeasurement(feature, state, currentTime, "Measurement stopped: the gameplay scene or settings ownership changed.");
			else if (feature->IsPerformanceCostMeasurementReady() && feature->IsPerformanceCostMeasurementEnabled() != expectsEnabled)
				StopFeatureCostMeasurement(feature, state, currentTime, "Measurement stopped: the feature changed outside the on/off comparison.");
		}

		const auto prefixes = BuildProfilingPrefixesForFeature(shortName);
		const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(prefixes, true);
#ifdef DEVBENCH_BRIDGE_ENABLED
		const bool sweepMeasurement =
			g_upscalingCostSweep.phase == UpscalingCostSweepPhase::Measuring &&
			shortName == "Upscaling" &&
			g_upscalingCostSweep.currentCaseIndex < g_upscalingCostSweep.cases.size();
		RecordFeatureCostTrace(
			timing,
			currentTime,
			sweepMeasurement ? g_upscalingCostSweep.runStartTime : state.runStartTime,
			state.phaseStartTime,
			GetFeatureCostPhaseName(state.phase),
			sweepMeasurement ? g_upscalingCostSweep.cases[g_upscalingCostSweep.currentCaseIndex].id : shortName);
#endif

		UpdateFeatureCostMeasurement(feature, state, timing, currentTime);
		if (!IsUpscalingCostSweepRunning())
			CaptureSavedFeature(shortName, state);
		if (!IsFeatureCostMeasurementActive(state)) {
			shouldReopenMenu = shouldReopenMenu || reopenMenuOnCompletion;
			completedDevBenchMeasurement = completedDevBenchMeasurement || devBenchOwned;
		}
	}

	const bool batchWasActive = g_featureCostBatch.active;
	shouldReopenMenu = UpdateFeatureCostBatch(currentTime) || shouldReopenMenu;
	const bool batchCompleted = batchWasActive && !g_featureCostBatch.active;
	UpdateUpscalingCostSweep(currentTime);
	UpdateSavedRun();
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (IsUpscalingCostSweepRunning() && !IsAnyFeatureCostMeasurementActive()) {
		const auto timing = ProfilingRenderer::CapturePerformanceTimingSummary(
			BuildProfilingPrefixesForFeature("Upscaling"),
			true);
		RecordFeatureCostTrace(
			timing,
			currentTime,
			g_upscalingCostSweep.runStartTime,
			g_upscalingCostSweep.phaseStartTime,
			GetUpscalingCostSweepPhaseName(g_upscalingCostSweep.phase),
			GetUpscalingCostSweepTraceCaseId());
	}

#endif

	SyncFeatureCostVanityCameraSuppression();
	if ((completedDevBenchMeasurement || shouldReopenMenu || batchCompleted) && !HasActiveMeasurements())
		RestoreProfilerStateAfterPerformanceTuning();
	if (shouldReopenMenu && !HasActiveMeasurements() &&
		globals::menu && !globals::menu->IsEnabled) {
		globals::menu->OpenMenu();
	}
}

void PerformanceTuningRenderer::RenderClosedMenuMeasurementOverlay()
{
	const FeatureCostMeasurementState* activeState = nullptr;
	Feature* activeFeature = nullptr;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (!IsFeatureCostMeasurementActive(state))
			continue;

		activeState = &state;
		activeFeature = FindFeatureByShortName(shortName);
		break;
	}
	const bool sweepRunning = IsUpscalingCostSweepRunning();
	if (!activeState && !sweepRunning && !g_featureCostBatch.active && !g_quickScan.controller.Active())
		return;

	const double currentTime = ImGui::GetTime();
	double remainingSeconds = 1.0;
	float caseProgress = 0.0f;
	if (activeState) {
		const double estimatedTotalSeconds = activeState->waits.TotalSeconds(kFeatureCostMeasurementSeconds);
		remainingSeconds = GetFeatureCostRemainingSeconds(*activeState, currentTime);
		caseProgress = static_cast<float>(std::clamp(
			1.0 - remainingSeconds / estimatedTotalSeconds,
			0.0,
			0.99));
	} else if (g_featureCostBatch.active) {
		remainingSeconds = std::max(1.0, GetFeatureCostRestartCooldownRemaining(currentTime));
	} else if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::InterCaseCooldown) {
		remainingSeconds = std::max(1.0, GetFeatureCostRestartCooldownRemaining(currentTime));
	} else if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::RestoringOriginal) {
		remainingSeconds = std::max(
			1.0,
			kFeatureCostRestoreWaitSeconds - (currentTime - g_upscalingCostSweep.phaseStartTime));
		caseProgress = 0.99f;
	}

	if (g_quickScan.controller.Active()) {
		const bool capturing = g_quickScan.controller.phase == PerformanceQuickScan::Phase::Capturing;
		remainingSeconds = capturing ? 0.0 : std::max(0.0, PerformanceQuickScan::kSettleSeconds - (currentTime - g_quickScan.controller.readySince));
		if (g_quickScan.controller.phase == PerformanceQuickScan::Phase::BetweenScans)
			remainingSeconds = std::max(remainingSeconds, PerformanceQuickScan::kBatchGapSeconds - (currentTime - g_quickScan.controller.started));
		caseProgress = capturing ? static_cast<float>(g_quickScan.resolvedFrames) / PerformanceQuickScan::kCaptureFrames : 0.0f;
	}
	float progress = caseProgress;
	if (g_quickScan.controller.Active())
		progress = (g_quickScan.controller.scanIndex - 1 + caseProgress) / g_quickScan.controller.scanCount;
	if (g_featureCostBatch.active && !g_featureCostBatch.features.empty()) {
		const std::size_t completed = g_featureCostBatch.nextFeatureIndex - (activeState ? 1 : 0);
		progress = static_cast<float>((static_cast<double>(completed) + caseProgress) / static_cast<double>(g_featureCostBatch.features.size()));
	}
	if (sweepRunning && !g_upscalingCostSweep.cases.empty()) {
		progress = static_cast<float>(std::clamp(
			(static_cast<double>(g_upscalingCostSweep.currentCaseIndex) + caseProgress) /
				static_cast<double>(g_upscalingCostSweep.cases.size()),
			0.0,
			0.99));
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	if (!viewport)
		return;

	const float scale = Util::GetUIScale();
	const float horizontalPadding = 24.0f * scale;
	const float overlayWidth = std::min(
		360.0f * scale,
		std::max(220.0f * scale, viewport->WorkSize.x - horizontalPadding * 2.0f));
	ImGui::SetNextWindowPos(
		ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 32.0f * scale),
		ImGuiCond_Always,
		ImVec2(0.5f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(overlayWidth, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);
	constexpr ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("ActualFeatureCostProgress", nullptr, flags)) {
		if (g_quickScan.controller.Active()) {
			ImGui::Text("Quick scan %u / %u: %s", g_quickScan.controller.scanIndex, g_quickScan.controller.scanCount,
				g_quickScan.controller.phase == PerformanceQuickScan::Phase::Capturing ? "capturing" : g_quickScan.controller.phase == PerformanceQuickScan::Phase::BetweenScans ? "gap / settling" :
																																												   "settling");
			ImGui::Text("%u / %u frames", g_quickScan.resolvedFrames, PerformanceQuickScan::kCaptureFrames);
		} else if (sweepRunning) {
			if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::RestoringOriginal) {
				ImGui::TextUnformatted("Upscaling sweep: restoring settings");
			} else {
				const std::size_t displayedCase = std::min(
					g_upscalingCostSweep.currentCaseIndex + 1,
					g_upscalingCostSweep.cases.size());
				ImGui::Text(
					"Measuring Upscaling %zu/%zu",
					displayedCase,
					g_upscalingCostSweep.cases.size());
			}
		} else if (g_featureCostBatch.active) {
			ImGui::Text("Measuring features %zu/%zu", g_featureCostBatch.nextFeatureIndex, g_featureCostBatch.features.size());
			if (activeFeature)
				ImGui::TextUnformatted(activeFeature->GetDisplayName().c_str());
		} else if (activeFeature) {
			ImGui::Text("Measuring %s", activeFeature->GetDisplayName().c_str());
		} else {
			ImGui::TextUnformatted("Measuring");
		}
		ImGui::TextColored(Util::Colors::GetWarning(), "Keep still until measurement completes.");
		if ((g_quickScan.controller.Active() && !g_quickScan.devBenchOwned) || (g_featureCostBatch.active && !g_featureCostBatch.devBenchOwned) || (activeState && !activeState->devBenchOwned))
			ImGui::TextWrapped("Use the menu shortcut to cancel.");
		std::string progressText;
		if (g_quickScan.controller.phase == PerformanceQuickScan::Phase::Capturing)
			progressText = "Capturing";
		else if (g_featureCostBatch.active)
			progressText = fmt::format("{:.0f}s {}", std::ceil(remainingSeconds), activeState ? "for this feature" : "until next feature");
		else
			progressText = fmt::format("{:.0f} seconds remaining", std::ceil(remainingSeconds));
		ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), progressText.c_str());
	}
	ImGui::End();
}

bool PerformanceTuningRenderer::CancelUserMeasurements()
{
	if (g_quickScan.controller.Active() && !g_quickScan.devBenchOwned) {
		FinishQuickScan(PerformanceQuickScan::Phase::Cancelled, "Quick scan cancelled.", false);
		return true;
	}
	return CancelOwnedFeatureMeasurements(false);
}

void PerformanceTuningRenderer::NotifyConfigurationChanging()
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	ABComparisonRunner::Get().ConfigurationReplacing();
#endif
	// Restore owned state before a replacement configuration can take ownership.
	if (HasActiveMeasurements())
		CancelActiveMeasurements();
	g_quickScan = {};
	g_costMeasurementStates.clear();
	g_disabledFeatureConfigurations.clear();
	g_featureCostBatch = {};
	g_upscalingCostSweep = {};
	g_featureCostUiMessage.clear();
	failedToggleFeature = nullptr;
	ResetFeatureCostTrace();
}

void PerformanceTuningRenderer::CancelActiveMeasurements()
{
	if (g_quickScan.controller.Active())
		FinishQuickScan(PerformanceQuickScan::Phase::Cancelled, "Quick scan cancelled because the configuration changed.", false);
	FinishSavedRun("cancelled", "Cancelled because the configuration changed.");
	const bool sweepRunning = IsUpscalingCostSweepRunning();
	g_featureCostBatch.active = false;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (sweepRunning && shortName == "Upscaling") {
			state = {};
			continue;
		}
		CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state);
	}
	if (sweepRunning) {
		globals::features::upscaling.RestorePerformanceCostMeasurementState(g_upscalingCostSweep.originalState);
		g_upscalingCostSweep.phase = UpscalingCostSweepPhase::Cancelled;
		g_upscalingCostSweep.failureMessage = "Cancelled because the configuration changed.";
	}
	SyncFeatureCostVanityCameraSuppression();
	RestoreProfilerStateAfterPerformanceTuning();
}

void PerformanceTuningRenderer::NotifyMenuClosed()
{
	const double currentTime = ImGui::GetTime();
	g_quickScan.controller.MenuClosed(currentTime);
	bool startedMeasurement = false;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (state.phase != FeatureCostMeasurementPhase::AwaitingMenuClose)
			continue;

		if (!FindFeatureByShortName(shortName)) {
			state.phase = FeatureCostMeasurementPhase::Complete;
			state.failureMessage = "The feature became unavailable before measurement.";
			CaptureSavedFeature(shortName, state);
			continue;
		}

		state.runStartTime = currentTime;
		PrepareFeatureCostPhase(
			FeatureCostMeasurementPhase::PreparingCurrent,
			state,
			currentTime,
			state.waits.current);
		startedMeasurement = true;
	}
	if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::AwaitingMenuClose) {
		g_upscalingCostSweep.runStartTime = currentTime;
		g_upscalingCostSweep.phaseStartTime = currentTime;
		if (StartCurrentUpscalingCostSweepCase(currentTime)) {
			startedMeasurement = true;
		} else {
			BeginUpscalingCostSweepRestore(
				currentTime,
				UpscalingCostSweepPhase::Failed,
				"The first Upscaling measurement could not start after CS closed.");
		}
	}
	SyncFeatureCostVanityCameraSuppression();
	if (!startedMeasurement && !HasActiveMeasurements())
		RestoreProfilerStateAfterPerformanceTuning();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (ABComparisonRunner::Get().IsRunning())
		return true;
#endif
	return g_quickScan.controller.Active() || IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning() || g_featureCostBatch.active;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json PerformanceTuningRenderer::CaptureComparisonSettings()
{
	auto result = CaptureQuickScanSettings();
	result["$blockedShader"] = globals::shaderCache->blockedKeyIndex;
	result["$shaderDefinesGeneration"] = globals::state->GetShaderDefinesGeneration();
	result["$shadersRequested"] = globals::shaderCache->IsEnableRequested();
	return result;
}

const char* PerformanceTuningRenderer::GetComparisonEnvironmentError()
{
	return GetFeatureCostEnvironmentError();
}

const char* PerformanceTuningRenderer::GetComparisonFeatureBlockReason(Feature* feature)
{
	if (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement())
		return "feature has no reversible runtime comparison state";
	const auto name = feature->GetShortName();
	if (name == "Upscaling" || name == "VR")
		return "use the dedicated upscaling/depth controls; these states share render-scale or other feature ownership";
	for (const auto& [setting, constraint] : FeatureConstraints::GetAllActiveConstraints()) {
		if (setting.featureShortName == name && constraint.isConstrained)
			return "another feature controls this feature's settings";
	}
	return GetFeatureToggleBlockReason(feature);
}

#endif

nlohmann::json PerformanceTuningRenderer::StartApiFeatureCostMeasurement(
	std::string_view a_featureShortName)
{
	const double currentTime = ImGui::GetTime();
	json response = {
		{ "action", "start_feature_cost" },
		{ "accepted", false },
		{ "feature", std::string(a_featureShortName) },
	};
	const auto reject = [&](const char* errorCode) {
		response["errorCode"] = errorCode;
		response["status"] = BuildMeasurementStatus(0, 128);
		return response;
	};
	if (a_featureShortName.empty())
		return reject("feature_required");

	const auto features = BuildPerformanceFeatureList();
	const auto featureIt = std::ranges::find_if(features, [&](Feature* feature) {
		return feature && feature->GetShortName() == a_featureShortName;
	});
	if (featureIt == features.end())
		return reject("feature_unavailable");

	auto* feature = *featureIt;
	if (!feature->SupportsPerformanceCostMeasurement())
		return reject("measurement_unsupported");
	if (const char* error = GetFeatureCostStartError(currentTime))
		return reject(error);
	if (!feature->IsPerformanceCostMeasurementEnabled())
		return reject("feature_inactive");
	if (!feature->IsPerformanceCostMeasurementReady())
		return reject("feature_not_ready");
	if (GetFeatureToggleBlockReason(feature))
		return reject("feature_controlled");

	auto& state = g_costMeasurementStates[feature->GetShortName()];
	if (!BeginFeatureCostMeasurement(
			feature,
			state,
			currentTime,
			feature->CapturePerformanceCostMeasurementState(),
			true,
			true,
			true)) {
		return reject("measurement_start_failed");
	}

	SyncFeatureCostVanityCameraSuppression();
	response["accepted"] = true;
	response["status"] = BuildMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartApiQuickScan(bool batch, bool recordFpsVR)
{
	const auto* error = StartQuickScan(true, batch, recordFpsVR);
	json response = { { "action", batch ? "start_quick_scan_batch" : "start_quick_scan" }, { "accepted", error == nullptr } };
	if (error) {
		response["errorCode"] = error;
		response["message"] = QuickScanStartFailureMessage(error, batch);
	}
	response["status"] = BuildMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartApiFeatureCostBatch()
{
	const char* error = StartFeatureCostBatch(true);
	json response = { { "action", "start_feature_costs" }, { "accepted", error == nullptr } };
	if (error)
		response["errorCode"] = error;
	response["status"] = BuildMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::SetApiFeatureEnabled(std::string_view a_featureShortName, bool a_enabled)
{
	json response = { { "action", "set_feature_enabled" }, { "accepted", false }, { "feature", a_featureShortName } };
	auto* feature = FindFeatureByShortName(a_featureShortName);
	if (HasActiveMeasurements())
		response["errorCode"] = "measurement_busy";
	else if (!feature || !feature->loaded || !feature->SupportsPerformanceCostMeasurement())
		response["errorCode"] = "feature_unavailable";
	else if (!globals::state || globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen)
		response["errorCode"] = "not_in_game";
	else if (GetFeatureToggleBlockReason(feature))
		response["errorCode"] = "feature_controlled";
	else if (!SetRuntimeFeatureEnabled(feature, a_enabled)) {
		response["errorCode"] = "feature_toggle_failed";
		if (const auto* reason = GetFeatureToggleFailureReason(feature))
			response["message"] = reason;
	} else
		response["accepted"] = true;
	response["status"] = BuildMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartApiUpscalingCostSweep(
	std::string_view a_matrix,
	std::string_view a_dlssPreset)
{
	const double currentTime = ImGui::GetTime();
	json response = {
		{ "action", "start_upscaling_sweep" },
		{ "accepted", false },
		{ "requestedMatrix", std::string(a_matrix) },
	};
	if (a_matrix != "auto" && a_matrix != "nvidia" && a_matrix != "amd") {
		response["errorCode"] = "invalid_matrix";
		response["supportedMatrices"] = json::array({ "auto", "nvidia", "amd" });
		response["status"] = BuildMeasurementStatus(0, 128);
		return response;
	}
	const auto readiness = CaptureUpscalingCostSweepReadiness(currentTime);
	if (const char* errorCode = GetUpscalingCostSweepReadinessError(readiness)) {
		response["errorCode"] = errorCode;
		response["status"] = BuildMeasurementStatus(0, 128);
		return response;
	}

	auto& upscaling = globals::features::upscaling;
	UpscalingCostSweepMatrix matrix = UpscalingCostSweepMatrix::Nvidia;
	const bool amdAdapter = Upscaling::fidelityFX.IsAmdAdapterDetected();
	const bool fsr4Available = IsFsr4UpscalingCostSweepAvailable();
	if (a_matrix == "amd" || (a_matrix == "auto" && amdAdapter)) {
		if (!amdAdapter) {
			response["errorCode"] = "amd_adapter_required";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
		if (!fsr4Available) {
			response["errorCode"] = "fsr4_unavailable";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
		matrix = UpscalingCostSweepMatrix::Amd;
	} else {
		if (!Upscaling::fidelityFX.IsNvidiaAdapterDetected()) {
			response["errorCode"] = "nvidia_adapter_required";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::streamline.featureCheckComplete) {
			response["errorCode"] = "dlss_capability_pending";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::streamline.featureDLSS) {
			response["errorCode"] = "dlss_unavailable";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
	}

	uint32_t dlssPreset = Upscaling::kDLSSPresetK;
	if (matrix == UpscalingCostSweepMatrix::Nvidia) {
		if (a_dlssPreset.empty()) {
			response["promptRequired"] = true;
			response["prompt"] = "Choose one DLSS profile for the sweep (J, K, L, M, F, or E).";
			response["allowedDlssPresets"] = GetDLSSPresetChoicesJson();
			response["matrix"] = "nvidia";
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::TryParseDLSSPresetName(a_dlssPreset, dlssPreset)) {
			response["errorCode"] = "invalid_dlss_preset";
			response["allowedDlssPresets"] = GetDLSSPresetChoicesJson();
			response["status"] = BuildMeasurementStatus(0, 128);
			return response;
		}
	} else if (!a_dlssPreset.empty()) {
		response["errorCode"] = "dlss_preset_not_applicable";
		response["status"] = BuildMeasurementStatus(0, 128);
		return response;
	}

	if (!BeginSavedRun("upscaling_sweep")) {
		response["errorCode"] = "results_busy";
		return response;
	}
	g_upscalingCostSweep = {};
	g_upscalingCostSweep.matrix = matrix;
	g_upscalingCostSweep.dlssPreset = dlssPreset;
	g_upscalingCostSweep.originalState = upscaling.CapturePerformanceCostMeasurementState();
	g_upscalingCostSweep.mainMenuWasOpen = globals::menu->IsEnabled;
	if (auto* editor = EditorWindow::GetSingleton())
		g_upscalingCostSweep.editorWasOpen = editor->open;
	g_upscalingCostSweep.runStartTime = currentTime;
	g_upscalingCostSweep.phaseStartTime = currentTime;
	g_upscalingCostSweep.cases = matrix == UpscalingCostSweepMatrix::Nvidia ?
	                                 BuildNvidiaUpscalingCostSweepCases(
										 g_upscalingCostSweep.originalState,
										 dlssPreset) :
	                                 BuildAmdUpscalingCostSweepCases(
										 g_upscalingCostSweep.originalState);
	ResetFeatureCostTrace();
	CaptureProfilerStateForPerformanceTuning();
	bool started = false;
	if (g_upscalingCostSweep.mainMenuWasOpen || g_upscalingCostSweep.editorWasOpen) {
		g_upscalingCostSweep.phase = UpscalingCostSweepPhase::AwaitingMenuClose;
		SyncFeatureCostVanityCameraSuppression();
		globals::menu->CloseMenu();
		started = g_upscalingCostSweep.phase == UpscalingCostSweepPhase::AwaitingMenuClose ||
		          g_upscalingCostSweep.phase == UpscalingCostSweepPhase::Measuring;
	} else {
		started = StartCurrentUpscalingCostSweepCase(currentTime);
	}
	if (!started) {
		if (g_upscalingCostSweep.phase != UpscalingCostSweepPhase::RestoringOriginal) {
			BeginUpscalingCostSweepRestore(
				currentTime,
				UpscalingCostSweepPhase::Failed,
				"The first Upscaling measurement could not start.");
		}
		response["errorCode"] = "measurement_start_failed";
		response["status"] = BuildMeasurementStatus(0, 128);
		return response;
	}

	SyncFeatureCostVanityCameraSuppression();
	response["accepted"] = true;
	response["status"] = BuildMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::GetApiMeasurementStatus(
	std::uint64_t a_traceAfterSequence,
	std::size_t a_maximumTraceSamples)
{
	return BuildMeasurementStatus(a_traceAfterSequence, a_maximumTraceSamples);
}

std::string PerformanceTuningRenderer::GetApiMeasurementRunId()
{
	return g_savedRun.is_object() ? g_savedRun.value("id", std::string{}) : std::string{};
}

nlohmann::json PerformanceTuningRenderer::CancelApiMeasurements()
{
	const double currentTime = ImGui::GetTime();
	bool cancelled = false;
	if (g_quickScan.controller.Active() && g_quickScan.devBenchOwned) {
		FinishQuickScan(PerformanceQuickScan::Phase::Cancelled, "Quick scan cancelled.");
		cancelled = true;
	}
	cancelled = CancelUpscalingCostSweep(currentTime) || cancelled;
	cancelled = CancelOwnedFeatureMeasurements(true) || cancelled;
	return {
		{ "action", "cancel" },
		{ "accepted", cancelled },
		{ "status", BuildMeasurementStatus(0, 128) },
	};
}
