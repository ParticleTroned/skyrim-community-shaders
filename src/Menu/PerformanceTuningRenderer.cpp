#include "PerformanceTuningRenderer.h"
#include "Menu/SettingsPage.h"

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
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceTuningController.h"
#include "Menu/PerformanceTuningStatistics.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/UI.h"
#include "Utils/VanityCamera.h"

namespace
{
	constexpr double kFeatureCostMeasurementSeconds = 5.0;
	constexpr double kFeatureCostMeasurementMilliseconds = kFeatureCostMeasurementSeconds * 1000.0;
	constexpr double kFeatureCostIntervalMilliseconds = 1000.0;
	constexpr double kFeatureCostInitialWaitSeconds = 10.0;
	constexpr double kFeatureCostComparisonWaitSeconds = 10.0;
	constexpr double kFeatureCostRestoreWaitSeconds = 1.0;
	constexpr double kFeatureCostRestartCooldownSeconds = 10.0;
	constexpr double kFeatureCostMaximumRunSeconds = 45.0;
	constexpr std::size_t kFeatureCostMaximumMissingMetricSamples = 2;
	constexpr double kFeatureCostTraceIntervalSeconds = 0.1;
	constexpr std::size_t kFeatureCostTraceCapacity = 8192;
	constexpr std::size_t kFeatureCostMaximumTracePageSize = 512;
	constexpr std::size_t kFeatureCostMeasurementBlockCount =
		static_cast<std::size_t>(kFeatureCostMeasurementMilliseconds / kFeatureCostIntervalMilliseconds);
	static_assert(
		kFeatureCostMeasurementBlockCount == 5 &&
		kFeatureCostMeasurementBlockCount * kFeatureCostIntervalMilliseconds == kFeatureCostMeasurementMilliseconds);

	constexpr std::array<std::string_view, 21> kPerformanceFeatureOrder = {
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
		"TruePBR",
		"ExtendedMaterials",
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

	struct FeatureCostTraceSample
	{
		std::uint64_t sequence = 0;
		double runElapsedMs = 0.0;
		double phaseElapsedMs = 0.0;
		std::uint32_t frameCount = 0;
		std::uint32_t sampleFrameCount = 0;
		std::uint64_t samplePresentId = 0;
		bool flatTiming = false;
		float frameMs = 0.0f;
		float gameGpuMs = 0.0f;
		float gameCpuMs = 0.0f;
		bool hasFrame = false;
		bool hasGameGpu = false;
		bool hasGameCpu = false;
		std::string phase;
		std::string caseId;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static UpscalingCostSweepState g_upscalingCostSweep;
	static std::deque<FeatureCostTraceSample> g_featureCostTrace;
	static std::uint64_t g_featureCostTraceNextSequence = 1;
	static double g_featureCostTraceLastTime = -1.0;
	static double g_costMeasurementRestartAllowedTime = 0.0;
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
		RE::FormID cellFormId = 0;
		std::vector<std::string> features;
		std::string failureMessage;
	};

	static std::unordered_map<std::string, std::optional<DisabledFeatureConfiguration>> g_disabledFeatureConfigurations;
	static FeatureCostBatchState g_featureCostBatch;
	static std::string g_featureCostUiMessage;

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
		g_costMeasurementRestartAllowedTime = currentTime + kFeatureCostRestartCooldownSeconds;
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
			.idle = !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning() && !g_featureCostBatch.active,
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
		if (IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning() || g_featureCostBatch.active)
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
		g_featureCostTrace.clear();
		g_featureCostTraceLastTime = -1.0;
	}

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
		sample.sampleFrameCount = summary.flatTiming ? summary.sampleFrameCount : summary.frameCount;
		sample.samplePresentId = summary.samplePresentId;
		sample.flatTiming = summary.flatTiming;
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

	FeatureCostSampleResult AddFeatureCostSample(
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

	double GetFeatureCostExpectedRunSeconds(const Feature* feature)
	{
		return kFeatureCostInitialWaitSeconds +
		       kFeatureCostMeasurementSeconds +
		       GetFeatureCostComparisonWaitSeconds(feature) +
		       kFeatureCostMeasurementSeconds +
		       kFeatureCostRestoreWaitSeconds;
	}

	double GetFeatureCostRemainingSeconds(
		const FeatureCostMeasurementState& state,
		const Feature* feature,
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
		const double comparisonWaitSeconds = GetFeatureCostComparisonWaitSeconds(feature);
		double remainingSeconds = 0.0;
		switch (state.phase) {
		case FeatureCostMeasurementPhase::AwaitingMenuClose:
			remainingSeconds = GetFeatureCostExpectedRunSeconds(feature);
			break;
		case FeatureCostMeasurementPhase::PreparingCurrent:
			remainingSeconds = remainingWait() +
			                   kFeatureCostMeasurementSeconds +
			                   comparisonWaitSeconds +
			                   kFeatureCostMeasurementSeconds +
			                   kFeatureCostRestoreWaitSeconds;
			break;
		case FeatureCostMeasurementPhase::MeasuringCurrent:
			remainingSeconds = remainingSample(state.currentSample) +
			                   comparisonWaitSeconds +
			                   kFeatureCostMeasurementSeconds +
			                   kFeatureCostRestoreWaitSeconds;
			break;
		case FeatureCostMeasurementPhase::PreparingTest:
			remainingSeconds = remainingWait() +
			                   kFeatureCostMeasurementSeconds +
			                   kFeatureCostRestoreWaitSeconds;
			break;
		case FeatureCostMeasurementPhase::MeasuringTest:
			remainingSeconds = remainingSample(state.testSample) +
			                   kFeatureCostRestoreWaitSeconds;
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
			return "Pause this feature's scene-specific settings before toggling or measuring it.";
		return nullptr;
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
			logger::warn("Actual feature cost measurement was not started because the 10-second restart cooldown is active");
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

		CaptureProfilerStateForPerformanceTuning();
		if (resetTrace)
			ResetFeatureCostTrace();
		state = {};
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
				kFeatureCostInitialWaitSeconds);
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
			PrepareFeatureCostPhase(FeatureCostMeasurementPhase::Restoring, state, currentTime, kFeatureCostRestoreWaitSeconds);
		} else {
			state.phase = FeatureCostMeasurementPhase::Complete;
			StartFeatureCostRestartCooldown(currentTime);
		}
	}

	void BeginFeatureCostSampleWindow(
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		sample = {};
		// The frame which completed preparation belongs to the wait period.
		sample.lastFrameCount = current.frameCount;
		state.phase = phase;
		state.phaseStartTime = currentTime;
	}

	bool RestartInterruptedFeatureCostSample(
		FeatureCostSampleResult result,
		FeatureCostSample& sample,
		uint32_t currentFrameCount,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (result != FeatureCostSampleResult::Interrupted)
			return false;

		sample = {};
		sample.lastFrameCount = currentFrameCount;
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
					kFeatureCostInitialWaitSeconds);
				return;
			}
			const auto sampleResult = AddFeatureCostSample(state.currentSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.currentSample,
					current.frameCount,
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
					GetFeatureCostComparisonWaitSeconds(feature));
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
					kFeatureCostInitialWaitSeconds);
				return;
			}
			const auto sampleResult = AddFeatureCostSample(state.testSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.testSample,
					current.frameCount,
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
					kFeatureCostRestoreWaitSeconds);
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
			result.label = fmt::format("{} {}", fsr4RuntimeEnabled ? "FSR4" : "FSR3", qualityName);
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
						"The FSR4 provider fell back before its measurement could complete." :
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

	void RenderMetricCounter(const char* label, float value, const char* format, bool valid)
	{
		ImGui::PushID(label);
		const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
		const float font = ImGui::GetFontSize();
		auto background = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
		background.w *= MenuUI::SettingsSurfaceOpacityScale;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { font * .75f, font * .6f });
		const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleVar(); ImGui::PopStyleColor(); });
		const float height = font * 3.8f + ImGui::GetStyle().ItemSpacing.y;
		const bool visible = ImGui::BeginChild("##Counter", { 0, height }, ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const SKSE::stl::scope_exit endChild([] { ImGui::EndChild(); });
		if (!visible)
			return;
		ImGui::TextColored(Util::Color::SecondaryText(), "%s", label);
		char text[64];
		std::snprintf(text, sizeof(text), valid ? format : "--", value);
		const float textWidth = ImGui::GetFont()->CalcTextSizeA(font * 1.6f, FLT_MAX, 0, text).x;
		const float textScale = std::min(1.0f, ImGui::GetContentRegionAvail().x / std::max(1.0f, textWidth));
		ImGui::PushFont(ImGui::GetFont(), font * 1.6f * textScale);
		const SKSE::stl::scope_exit restoreFont([] { ImGui::PopFont(); });
		if (valid)
			ImGui::TextUnformatted(text);
		else
			ImGui::TextDisabled("%s", text);
	}

	void RenderTopPerformanceCounters(const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		float displayGpuMs = 0.0f;
		const bool hasDisplayGpu = TryGetDisplayGpuMs(summary, displayGpuMs);
		float displayCpuMs = 0.0f;
		const bool hasDisplayCpu = TryGetDisplayCpuMs(summary, displayCpuMs);

		const float minimumWidth = ImGui::GetFontSize() * 7;
		const int columns = ImGui::GetContentRegionAvail().x >= minimumWidth * 4 ? 4 : 2;
		MenuUI::DetailGrid counters("##PerformanceTuningTopCounters", columns, minimumWidth);
		counters.Next();
		RenderMetricCounter("Game", summary.frameMs, "%.2f ms", summary.frameMs > 0.0f);
		counters.Next();
		RenderMetricCounter("GPU", displayGpuMs, "%.2f ms", hasDisplayGpu);
		counters.Next();
		RenderMetricCounter("CPU", displayCpuMs, "%.2f ms", hasDisplayCpu);
		counters.Next();
		RenderMetricCounter("FPS", summary.fps, "%.0f", summary.fps > 0.0f);
	}

	bool RenderMeasureButton(bool canStart)
	{
		auto guard = Util::DisableGuard(!canStart);
		return ImGui::Button("Measure", { ImGui::GetFontSize() * 10, ImGui::GetFrameHeight() * 1.4f });
	}

	void RenderMeasurementStatus(bool running)
	{
		const double cooldown = GetFeatureCostRestartCooldownRemaining(ImGui::GetTime());
		if (running)
			MenuUI::DetailText("Running with CS closed");
		else if (PerformanceTuningRenderer::HasActiveMeasurements())
			MenuUI::DetailText("Finish the current measurement first");
		else if (cooldown > 0.0)
			MenuUI::DetailText(fmt::format("Ready in {:.0f}s", std::ceil(cooldown)).c_str());
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
			return "all Adaptive Balance Lighting, Bloom, Water appearance, profile/location, and wind contributions are bypassed together.";
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
		if (shortName == "SubsurfaceScattering")
			return "Subsurface Scattering is switched off.";
		if (shortName == "TruePBR")
			return "True PBR material shading is switched off.";
		if (shortName == "ExtendedMaterials")
			return "complex materials, parallax, legacy terrain parallax, height blending, parallax shadows, and curvature correction are switched off.";
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
		MenuUI::DetailText(fmt::format("Compare current settings with {}. Your settings are restored after the run.", GetFeatureCostComparisonLabel(feature)).c_str());
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
			ImGui::TextWrapped("CS closes automatically for the complete run. Keep the headset and scene still for about 31 seconds; a small overlay shows progress and CS reopens with the results.");
			ImGui::TextWrapped("After a ten-second cooldown following menu closure, current settings are measured as five one-second intervals. The feature then changes to Off/None, waits ten seconds, and measures five more one-second intervals before restoring the exact prior state for one second.");
			ImGui::TextWrapped("If game-frame timing is interrupted during capture, only that five-second measurement restarts.");
			ImGui::TextWrapped("GPU and CPU rows tolerate up to two missing raw samples across both states. Three or more make only that row unavailable; missing data never blocks Game or FPS.");
			ImGui::TextWrapped("The automatic idle/vanity camera remains suppressed for the complete run and its previous delay is restored afterward.");
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
			MenuUI::DetailText("No measurement yet. Choose Measure to compare this feature in the current scene.");
			return;
		}

		if (!state.failureMessage.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
			ImGui::TextWrapped("%s", state.failureMessage.c_str());
			ImGui::PopStyleColor();
			return;
		}

		if (!state.delta.frame.available && !state.delta.fps.available &&
			!state.delta.gameGpu.available && !state.delta.gameCpu.available) {
			MenuUI::DetailText("No game timing data");
			return;
		}

		ImGui::Spacing();
		const std::string differenceHeader = fmt::format(
			"Current - {} \xC2\xB1 SE",
			GetFeatureCostComparisonLabel(feature));
		if (ImGui::BeginTable(
				"##FeatureCostResults",
				3,
				ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersInnerH |
					ImGuiTableFlags_PadOuterX |
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, std::ceil(ImGui::CalcTextSize("Metric").x));
			ImGui::TableSetupColumn(differenceHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Cost / FPS loss (%)", ImGuiTableColumnFlags_WidthStretch);
			RenderMeasurementTableHeaders();
			RenderFeatureCostMetricRow(
				"Game",
				state.delta.frame,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.frame.value),
				false);
			RenderFeatureCostMetricRow(
				"FPS",
				state.delta.fps,
				GetDirectionFromFeatureCostFpsDelta(state.delta.fps.value),
				true);
			RenderFeatureCostMetricRow(
				"GPU",
				state.delta.gameGpu,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameGpu.value),
				false);
			RenderFeatureCostMetricRow(
				"CPU",
				state.delta.gameCpu,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameCpu.value),
				false);
			ImGui::EndTable();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::SecondaryText());
		const SKSE::stl::scope_exit restoreFootnoteColor([] { ImGui::PopStyleColor(); });
		ImGui::TextWrapped(
			"* p <= 0.05    \xE2\x80\xA0"
			"1/\xE2\x80\xA0"
			"2: raw samples missing; 3+ = --");
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
		for (auto& [_, state] : g_costMeasurementStates)
			ClearFinishedFeatureCostMeasurement(state);
		if (!g_featureCostBatch.active)
			g_featureCostBatch = {};
		if (!IsUpscalingCostSweepRunning())
			g_upscalingCostSweep = {};
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
			!feature->IsPerformanceCostMeasurementEnabled())
			return false;
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

		InvalidateFeatureCostResults();
		ResetFeatureCostTrace();
		g_featureCostBatch.active = true;
		g_featureCostBatch.devBenchOwned = devBenchOwned;
		g_featureCostBatch.reopenMenuOnCompletion = globals::menu->IsEnabled;
		g_featureCostBatch.features = std::move(enabledFeatures);
		g_featureCostBatch.cellFormId = GetMeasurementCellId();
		if (!StartNextFeatureCostBatchMeasurement(currentTime)) {
			g_featureCostBatch.active = false;
			RestoreProfilerStateAfterPerformanceTuning();
			g_featureCostBatch.failureMessage = "The feature comparison could not start.";
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
			if (StartNextFeatureCostBatchMeasurement(currentTime))
				break;
			[[fallthrough]];
		case PerformanceTuningController::BatchAction::Fail:
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
			{ "fsrRuntime", isFSR ? json(fsr4RuntimeEnabled ? "FSR4" : "FSR3") : json(nullptr) },
		};
	}

	json UpscalingCostSweepResultJson(const UpscalingCostSweepCase& result)
	{
		return {
			{ "profile", UpscalingCostSweepProfileJson(result) },
			{ "relativeTo", "none" },
			{ "game", FeatureCostMetricDeltaJson(result.delta.frame, "ms") },
			{ "fps", FeatureCostMetricDeltaJson(result.delta.fps, "fps") },
			{ "gpu", FeatureCostMetricDeltaJson(result.delta.gameGpu, "ms") },
			{ "cpu", FeatureCostMetricDeltaJson(result.delta.gameCpu, "ms") },
		};
	}

	void AppendFlatTimingSource(json& timing, const FeatureCostTraceSample& sample)
	{
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
			AppendFlatTimingSource(samples.back(), sample);
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

	json BuildDevBenchMeasurementStatus(std::uint64_t traceAfterSequence, std::size_t maximumTraceSamples)
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

		json measurement = nullptr;
		if (activeMeasurement) {
			measurement = {
				{ "feature", activeFeature },
				{ "phase", GetFeatureCostPhaseName(activeMeasurement->phase) },
				{ "phaseElapsedMs", std::max(0.0, currentTime - activeMeasurement->phaseStartTime) * 1000.0 },
				{ "runElapsedMs", std::max(0.0, currentTime - activeMeasurement->runStartTime) * 1000.0 },
				{ "estimatedRemainingMs", GetFeatureCostRemainingSeconds(
											  *activeMeasurement,
											  FindFeatureByShortName(activeFeature),
											  currentTime) *
											  1000.0 },
			};
		}

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
			AppendFlatTimingSource(latestTiming, sample);
		}

		const bool sweepKnown = g_upscalingCostSweep.phase != UpscalingCostSweepPhase::Idle;
		const bool sweepRunning = IsUpscalingCostSweepRunning();
		const bool nvidiaSweep = sweepKnown &&
		                         g_upscalingCostSweep.matrix == UpscalingCostSweepMatrix::Nvidia;
		const auto readiness = CaptureUpscalingCostSweepReadiness(currentTime);
		const auto& fidelityFX = Upscaling::fidelityFX;
		json response = json::object();
		response["active"] = IsAnyFeatureCostMeasurementActive() || sweepRunning || g_featureCostBatch.active;
		response["featureBatch"] = {
			{ "active", g_featureCostBatch.active },
			{ "features", g_featureCostBatch.features },
			{ "startedCount", g_featureCostBatch.nextFeatureIndex },
			{ "failure", g_featureCostBatch.failureMessage.empty() ? json(nullptr) : json(g_featureCostBatch.failureMessage) },
		};
		response["owner"] = g_featureCostBatch.active ?
		                        (g_featureCostBatch.devBenchOwned ? "devbench_feature_batch" : "ui_feature_batch") :
		                    sweepRunning ?
		                        "devbench_upscaling_sweep" :
		                        (activeMeasurement ?
										(activeMeasurement->devBenchOwned ? "devbench_feature_cost" : "ui") :
										"none");
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
			{ "comparisonWaitMs", kFeatureCostComparisonWaitSeconds * 1000.0 },
			{ "postRunCooldownMs", kFeatureCostRestartCooldownSeconds * 1000.0 },
		};
		response["latestTiming"] = std::move(latestTiming);
		response["trace"] = FeatureCostTraceJson(traceAfterSequence, maximumTraceSamples);
		response["results"] = std::move(results);
		response["featureResults"] = std::move(featureResults);
		response["availableFeatureCosts"] = std::move(availableFeatureCosts);
		return response;
	}
}

void PerformanceTuningRenderer::RenderFeatureEnabledControl(Feature* a_feature)
{
	const bool supported = a_feature->SupportsPerformanceCostMeasurement();
	bool enabled = supported ? a_feature->IsPerformanceToggleEnabled() : a_feature->loaded;
	const auto* reason = GetFeatureToggleBlockReason(a_feature);
	const bool busy = HasActiveMeasurements();
	{
		auto guard = Util::DisableGuard(!a_feature->loaded || !supported || reason || busy);
		if (Util::Widgets::Checkbox("Enabled", &enabled)) {
			const bool applied = SetRuntimeFeatureEnabled(a_feature, enabled);
			g_featureCostUiMessage = applied ? "" : "This feature could not change state. Check its settings and runtime requirements.";
		}
	}
	Util::AddTooltip(reason ? reason : busy   ? "Wait for the current measurement to finish." :
								   !supported ? "This feature has no separate runtime switch. Use its sidebar switch, then restart the game." :
												"Turns this feature on or off while keeping your current tuning.");
}

void PerformanceTuningRenderer::Render()
{
	MenuUI::SettingsPage page("PerformanceTuning", { { "compare", "Compare total feature set", "Measure each enabled feature against Off or None in the current scene.", "Frame times, FPS and individual feature costs", true, true, "Compare your setup" } }, "Performance tuning", "Choose the comparison to measure your current feature set.");
	if (page.Is("compare"))
		RenderMeasurementSuite();
}

namespace
{
	void RenderFeatureSetMeasurement(const std::vector<Feature*>& features)
	{
		const bool anyEnabled = std::ranges::any_of(features, [](Feature* feature) {
			return feature->IsPerformanceCostMeasurementEnabled() && !GetFeatureToggleBlockReason(feature);
		});
		MenuUI::SectionHeading("Measurement");
		MenuUI::DetailText("Measure each active feature against Off or None, one at a time. Your settings are restored after every comparison.");
		if (RenderMeasureButton(GetFeatureCostStartError(ImGui::GetTime()) == nullptr && anyEnabled)) {
			const char* error = StartFeatureCostBatch(false);
			g_featureCostUiMessage = error ? "Measurement could not start. Check that a game is loaded, the editor is closed, and the features are ready." : "";
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Measure every active, editable feature against Off/None, one at a time. Inactive features and controls owned by scene or weather overrides are skipped. Each comparison restores its exact prior settings. CS closes for the complete run and reopens with the results. Keep the scene still; allow about 41 seconds per feature. Use the menu shortcut to cancel.");
		RenderMeasurementStatus(g_featureCostBatch.active);
		if (!g_featureCostUiMessage.empty())
			MenuUI::DetailText(g_featureCostUiMessage.c_str());
		if (!g_featureCostBatch.failureMessage.empty())
			MenuUI::DetailText(g_featureCostBatch.failureMessage.c_str());
		MenuUI::SectionHeading("Features and results");
		MenuUI::DetailText("Choose the enabled features below. These are individual on/off costs; percentages do not add up to a total. Unmeasured or unavailable results show --.");
		const bool busy = PerformanceTuningRenderer::HasActiveMeasurements();
		if (ImGui::BeginTable("##PerformanceFeatureCosts", 5,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Feature (in game toggle)", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Frame (%)");
			ImGui::TableSetupColumn("CPU (%)");
			ImGui::TableSetupColumn("GPU (%)");
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
					g_featureCostUiMessage = applied ? "" : "This feature could not change state. Check its settings and runtime requirements.";
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
	CaptureProfilerStateForPerformanceTuning();
	const auto features = a_feature ? std::vector<Feature*>{ a_feature } : BuildPerformanceFeatureList();
	RenderTopPerformanceCounters(ProfilingRenderer::CapturePerformanceTimingSummary(BuildPerformanceFeaturePrefixes(features), true));
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
	if (!a_feature || HasActiveMeasurements())
		return;
	InvalidateFeatureCostResults();
}

void PerformanceTuningRenderer::UpdateClosedMenuMeasurement()
{
	const double currentTime = ImGui::GetTime();
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
			state = {};
			continue;
		}

		if (currentTime - state.runStartTime >= kFeatureCostMaximumRunSeconds) {
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
		UpdateFeatureCostMeasurement(feature, state, timing, currentTime);
		if (!IsFeatureCostMeasurementActive(state)) {
			shouldReopenMenu = shouldReopenMenu || reopenMenuOnCompletion;
			completedDevBenchMeasurement = completedDevBenchMeasurement || devBenchOwned;
		}
	}

	const bool batchWasActive = g_featureCostBatch.active;
	shouldReopenMenu = UpdateFeatureCostBatch(currentTime) || shouldReopenMenu;
	const bool batchCompleted = batchWasActive && !g_featureCostBatch.active;
	UpdateUpscalingCostSweep(currentTime);
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
	if (!activeState && !sweepRunning && !g_featureCostBatch.active)
		return;

	const double currentTime = ImGui::GetTime();
	double remainingSeconds = 1.0;
	float caseProgress = 0.0f;
	if (activeState) {
		const double estimatedTotalSeconds = GetFeatureCostExpectedRunSeconds(activeFeature);
		remainingSeconds = GetFeatureCostRemainingSeconds(*activeState, activeFeature, currentTime);
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

	float progress = caseProgress;
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
		if (sweepRunning) {
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
		if ((g_featureCostBatch.active && !g_featureCostBatch.devBenchOwned) || (activeState && !activeState->devBenchOwned))
			ImGui::TextWrapped("Use the menu shortcut to cancel.");
		const std::string progressText = g_featureCostBatch.active ?
		                                     fmt::format("{:.0f}s {}", std::ceil(remainingSeconds), activeState ? "for this feature" : "until next feature") :
		                                     fmt::format("{:.0f} seconds remaining", std::ceil(remainingSeconds));
		ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), progressText.c_str());
	}
	ImGui::End();
}

bool PerformanceTuningRenderer::CancelUserMeasurements()
{
	return CancelOwnedFeatureMeasurements(false);
}

void PerformanceTuningRenderer::NotifyConfigurationChanging()
{
	// Restore owned state before a replacement configuration can take ownership.
	if (HasActiveMeasurements())
		CancelActiveMeasurements();
	g_costMeasurementStates.clear();
	g_disabledFeatureConfigurations.clear();
	g_featureCostBatch = {};
	g_upscalingCostSweep = {};
	g_featureCostUiMessage.clear();
	ResetFeatureCostTrace();
}

void PerformanceTuningRenderer::CancelActiveMeasurements()
{
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
	bool startedMeasurement = false;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (state.phase != FeatureCostMeasurementPhase::AwaitingMenuClose)
			continue;

		if (!FindFeatureByShortName(shortName)) {
			state = {};
			continue;
		}

		state.runStartTime = currentTime;
		PrepareFeatureCostPhase(
			FeatureCostMeasurementPhase::PreparingCurrent,
			state,
			currentTime,
			kFeatureCostInitialWaitSeconds);
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
	return IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning() || g_featureCostBatch.active;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchFeatureCostMeasurement(
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
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
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
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchFeatureCostBatch()
{
	const char* error = StartFeatureCostBatch(true);
	json response = { { "action", "start_feature_costs" }, { "accepted", error == nullptr } };
	if (error)
		response["errorCode"] = error;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::SetDevBenchFeatureEnabled(std::string_view a_featureShortName, bool a_enabled)
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
	else if (!SetRuntimeFeatureEnabled(feature, a_enabled))
		response["errorCode"] = "feature_toggle_failed";
	else
		response["accepted"] = true;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::StartDevBenchUpscalingCostSweep(
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
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
		return response;
	}
	const auto readiness = CaptureUpscalingCostSweepReadiness(currentTime);
	if (const char* errorCode = GetUpscalingCostSweepReadinessError(readiness)) {
		response["errorCode"] = errorCode;
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
		return response;
	}

	auto& upscaling = globals::features::upscaling;
	UpscalingCostSweepMatrix matrix = UpscalingCostSweepMatrix::Nvidia;
	const bool amdAdapter = Upscaling::fidelityFX.IsAmdAdapterDetected();
	const bool fsr4Available = IsFsr4UpscalingCostSweepAvailable();
	if (a_matrix == "amd" || (a_matrix == "auto" && amdAdapter)) {
		if (!amdAdapter) {
			response["errorCode"] = "amd_adapter_required";
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
		if (!fsr4Available) {
			response["errorCode"] = "fsr4_unavailable";
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
		matrix = UpscalingCostSweepMatrix::Amd;
	} else {
		if (!Upscaling::fidelityFX.IsNvidiaAdapterDetected()) {
			response["errorCode"] = "nvidia_adapter_required";
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::streamline.featureCheckComplete) {
			response["errorCode"] = "dlss_capability_pending";
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::streamline.featureDLSS) {
			response["errorCode"] = "dlss_unavailable";
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
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
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
		if (!Upscaling::TryParseDLSSPresetName(a_dlssPreset, dlssPreset)) {
			response["errorCode"] = "invalid_dlss_preset";
			response["allowedDlssPresets"] = GetDLSSPresetChoicesJson();
			response["status"] = BuildDevBenchMeasurementStatus(0, 128);
			return response;
		}
	} else if (!a_dlssPreset.empty()) {
		response["errorCode"] = "dlss_preset_not_applicable";
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
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
		response["status"] = BuildDevBenchMeasurementStatus(0, 128);
		return response;
	}

	SyncFeatureCostVanityCameraSuppression();
	response["accepted"] = true;
	response["status"] = BuildDevBenchMeasurementStatus(0, 128);
	return response;
}

nlohmann::json PerformanceTuningRenderer::GetDevBenchMeasurementStatus(
	std::uint64_t a_traceAfterSequence,
	std::size_t a_maximumTraceSamples)
{
	return BuildDevBenchMeasurementStatus(a_traceAfterSequence, a_maximumTraceSamples);
}

nlohmann::json PerformanceTuningRenderer::CancelDevBenchMeasurements()
{
	const double currentTime = ImGui::GetTime();
	bool cancelled = CancelUpscalingCostSweep(currentTime);
	cancelled = CancelOwnedFeatureMeasurements(true) || cancelled;
	return {
		{ "action", "cancel" },
		{ "accepted", cancelled },
		{ "status", BuildDevBenchMeasurementStatus(0, 128) },
	};
}
