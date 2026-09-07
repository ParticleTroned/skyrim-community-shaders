#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <imgui.h>
#include <imgui_internal.h>
#include <initializer_list>
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
#include "Menu/PerformanceTuningStatistics.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "SettingsSerialization.h"
#include "Utils/FileSystem.h"
#include "Utils/UI.h"
#include "Utils/VanityCamera.h"

namespace
{
	constexpr float kTuningDeltaThresholdMs = 0.099f;
	constexpr int kTuningSettleFrames = 20;
	constexpr int kTuningHighlightFrames = 240;
	constexpr double kFeatureCostMeasurementSeconds = 5.0;
	constexpr double kFeatureCostMeasurementMilliseconds = kFeatureCostMeasurementSeconds * 1000.0;
	constexpr double kFeatureCostIntervalMilliseconds = 1000.0;
	constexpr double kFeatureCostInitialWaitSeconds = 5.0;
	constexpr double kFeatureCostComparisonWaitSeconds = 9.0;
	constexpr double kFeatureCostRestoreWaitSeconds = 0.5;
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

	bool IsRenderScaleDesktopMirrorQualityAvailable()
	{
		return globals::game::isVR && globals::features::upscaling.IsVRRenderScaleModeActive();
	}

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
		"UnifiedWater",
		"Wetterness",
		"SubsurfaceScattering",
		"TruePBR",
		"ExtendedMaterials",
		"FoliageLighting",
		"GrassLighting",
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
	};

	struct FeatureCostMetricDelta
	{
		float value = 0.0f;
		float standardError = 0.0f;
		float pValue = 1.0f;
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
			.idle = !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning(),
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
		if (IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning())
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
		fps.value = static_cast<float>(currentFps - comparisonFps);
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

	ProfilingRenderer::PerformanceTimingTotals GetTimingTotalsForFeature(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const std::string& shortName);
	std::vector<std::string> BuildProfilingPrefixesForFeature(const std::string& shortName);
	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state);

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
				"SampleCount",
				"VRBaseSamplesAtReference",
				"VRCullDistance" });
		}
		if (shortName == "ScreenSpaceGI") {
			return MakeJsonMask({ "Enabled",
				"ResourceProfile",
				"AOInteriorsOnly",
				"VRCullDistance",
				"EnableAdaptiveSampling",
				"ResolutionMode",
				"NumSlices",
				"NumSteps" });
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
		if (shortName == "VR") {
			json mask = MakeJsonMask({ "EnableDepthBufferCullingExterior",
				"EnableDepthBufferCullingInterior",
				"DepthCullingPerformanceMode",
				"DepthCullingLegacyMode",
				"MinOccludeeBoxExtent",
				"EnableStereoBlend",
				"EnableLightingFoveation",
				"EnableSSRFoveation",
				"EnableWaterParallaxFoveation",
				"EnableWetternessFoveation",
				"EnableDynamicCubemapFoveation",
				"EnableDynamicCubemapVisibilityThrottle" });
			if (IsRenderScaleDesktopMirrorQualityAvailable())
				mask["StabilizeRenderScaleDesktopMirror"] = true;
			return mask;
		}
		if (shortName == "Wetterness") {
			json mask = currentSettings.is_object() ? currentSettings : json::object();
			mask.erase("DebugSettings");
			return mask;
		}
		if (shortName == "GrassLighting") {
			return MakeJsonMask({ "Enabled", "ComplexGrassThreshold" });
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
		std::string errorMessage;
		const auto result = Util::FileHelpers::ReadJsonFile(path, settings, errorMessage);
		if (result == Util::FileHelpers::JsonFileReadResult::NotFound)
			return true;
		if (result == Util::FileHelpers::JsonFileReadResult::Error) {
			logger::warn("Failed to read performance tuning user defaults from {}: {}", path.string(), errorMessage);
			settings = json::object();
			return false;
		}
		if (!settings.is_object()) {
			logger::warn("Performance tuning user defaults must contain a JSON object: {}", path.string());
			settings = json::object();
			return false;
		}

		return true;
	}

	bool WriteUserSettingsJson(const json& settings)
	{
		const auto path = Util::PathHelpers::GetSettingsUserPath();
		std::string errorMessage;
		if (!SettingsSerialization::WriteFileAtomic(path, settings, errorMessage)) {
			logger::warn("Failed to write performance tuning user defaults to {}: {}", path.string(), errorMessage);
			return false;
		}

		return true;
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
			return true;

		const auto shortName = feature->GetShortName();
		if (shortName == "VR") {
			bool ok = SaveFeatureSettingsToUserDefaults(
				FindFeatureByShortName("ScreenSpaceShadows"),
				userSettings,
				MakeJsonMask({ "EnableFoveated", "EnableStereoSync", "UseStereoReproject" }));
			ok = SaveFeatureSettingsToUserDefaults(
					 FindFeatureByShortName("ScreenSpaceGI"),
					 userSettings,
					 MakeJsonMask({ "EnableFoveated", "EnableStereoSync", "UseStereoReproject" })) &&
			     ok;
			return ok;
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

	bool ShouldRestoreRuntimePerformanceState(Feature* feature)
	{
		if (!feature)
			return false;

		const auto shortName = feature->GetShortName();
		return shortName == "Upscaling" ||
		       shortName == "Skylighting" ||
		       shortName == "VolumetricLighting" ||
		       shortName == "LightLimitFix" ||
		       shortName == "TerrainBlending" ||
		       shortName == "SubsurfaceScattering" ||
		       shortName == "ScreenSpaceGI" ||
		       shortName == "ExtendedMaterials";
	}

	void ApplyRestoredPerformanceRuntimeState(Feature* feature, const json& restoredSettings)
	{
		if (!feature)
			return;

		if (ShouldRestoreRuntimePerformanceState(feature)) {
			feature->RestorePerformanceCostMeasurementState(restoredSettings);
		}

		if (feature->GetShortName() == "VR") {
			feature->RestorePerformanceCostMeasurementState(restoredSettings);
			if (globals::features::vr.gMinOccludeeBoxExtent) {
				*globals::features::vr.gMinOccludeeBoxExtent = globals::features::vr.settings.MinOccludeeBoxExtent;
			}
		}
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
			ApplyRestoredPerformanceRuntimeState(feature, currentSettings);
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
		if (!feature)
			return;

		const auto shortName = feature->GetShortName();
		if (shortName == "VR") {
			RestoreFeatureSettingsFromUserDefaults(
				FindFeatureByShortName("ScreenSpaceShadows"),
				userSettings,
				MakeJsonMask({ "EnableFoveated", "EnableStereoSync", "UseStereoReproject" }),
				anyFound,
				anyChanged,
				anyFailed);
			RestoreFeatureSettingsFromUserDefaults(
				FindFeatureByShortName("ScreenSpaceGI"),
				userSettings,
				MakeJsonMask({ "EnableFoveated", "EnableStereoSync", "UseStereoReproject" }),
				anyFound,
				anyChanged,
				anyFailed);
		}
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
		if (!validGameGpuSample)
			RecordMissingFeatureCostSample(sample.gameGpu);
		if (!validGameCpuSample)
			RecordMissingFeatureCostSample(sample.gameCpu);

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
			if (validGameGpuSample)
				AddFeatureCostMoment(sample.gameGpu, blockIndex, summary.gameGpuSampleMs, chunkWeight);
			if (validGameCpuSample)
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

	bool BeginFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime,
		const json& originalState,
		bool allowClosedMenu,
		bool resetTrace,
		bool devBenchOwned)
	{
		if (!feature || !feature->SupportsPerformanceCostMeasurement() || !feature->IsPerformanceCostMeasurementEnabled())
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
		(void)BeginFeatureCostMeasurement(
			feature,
			state,
			currentTime,
			feature->CapturePerformanceCostMeasurementState(),
			false,
			true,
			false);
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

			FinalizeFeatureCostMeasurement(state);
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

	void RenderFeatureCostMetricRow(
		const char* label,
		const FeatureCostMetricDelta& metric,
		int direction,
		bool fps)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("%s", label);
		ImGui::TableSetColumnIndex(1);
		if (!metric.available || !metric.hasStandardError) {
			ImGui::TextDisabled("--");
			return;
		}

		const int colorDirection = metric.significant ? direction : 0;
		if (colorDirection != 0)
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::PerformanceDelta(colorDirection));

		const char* significanceMarker = metric.significant ? "*" : "";
		std::string missingSampleMarker;
		if (metric.missingSampleCount > 0)
			missingSampleMarker = fmt::format(" \xE2\x80\xA0{}", metric.missingSampleCount);
		if (fps) {
			ImGui::Text("%+.1f%s \xC2\xB1 %.1f%s", metric.value, significanceMarker, metric.standardError, missingSampleMarker.c_str());
		} else {
			ImGui::Text("%+.3f%s \xC2\xB1 %.3f ms%s", metric.value, significanceMarker, metric.standardError, missingSampleMarker.c_str());
		}

		if (colorDirection != 0)
			ImGui::PopStyleColor();
	}

	void RenderMetricCounter(const char* id, const char* label, float value, const char* format, int direction, bool valid)
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
			RenderMetricCounter("Game", "Game:", summary.frameMs, "%.2f ms", highlightState.frameDirection, summary.frameMs > 0.0f);
			ImGui::TableNextColumn();
			RenderMetricCounter("GPU", "GPU:", displayGpuMs, "%.2f ms", highlightState.gpuTotalDirection, hasDisplayGpu);
			ImGui::TableNextColumn();
			RenderMetricCounter("CPU", "CPU:", displayCpuMs, "%.2f ms", highlightState.cpuTotalDirection, hasDisplayCpu);
			ImGui::TableNextColumn();
			RenderMetricCounter("FPS", "FPS:", summary.fps, "%.0f", highlightState.fpsDirection, summary.fps > 0.0f);
			ImGui::EndTable();
		}
	}

	const char* GetFeatureCostComparisonLabel(Feature* feature)
	{
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
			return "Upscaling is set to None, with foveated upscaling disabled.";
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
		if (shortName == "UnifiedWater")
			return "optimized water meshes are switched off.";
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
		if (!feature->SupportsPerformanceCostMeasurement() && !hasMeasurementState)
			return;
		if (!feature->IsPerformanceCostMeasurementEnabled() && !hasMeasurementState)
			return;

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const bool running = IsFeatureCostMeasurementActive(state);
		const bool anyMeasurementRunning = IsAnyFeatureCostMeasurementActive();
		const double currentTime = ImGui::GetTime();
		const double restartCooldownRemaining =
			GetFeatureCostRestartCooldownRemaining(currentTime);
		const bool canStartMeasurement =
			feature->IsPerformanceCostMeasurementEnabled() &&
			!running &&
			!anyMeasurementRunning &&
			restartCooldownRemaining <= 0.0;
		ImGui::BeginDisabled(!canStartMeasurement);
		if (ImGui::Button("Actual feature cost")) {
			StartFeatureCostMeasurement(feature, state, currentTime);
		}
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("CS closes automatically for the complete run. Keep the headset and scene still for about 25 seconds; a small overlay shows progress and CS reopens with the results.");
			ImGui::TextWrapped("After a five-second cooldown following menu closure, current settings are measured as five one-second intervals. The feature then changes to Off/None, waits nine seconds, and measures five more one-second intervals before restoring the exact prior state.");
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
		if (restartCooldownRemaining > 0.0) {
			ImGui::SameLine();
			ImGui::TextDisabled("Ready in %.0fs", std::ceil(restartCooldownRemaining));
		}
		if (!running && anyMeasurementRunning && !IsFeatureCostMeasurementActive(state)) {
			ImGui::SameLine();
			ImGui::TextDisabled("Finish the current measurement first");
		}

		if (IsFeatureCostMeasurementActive(state)) {
			ImGui::SameLine();
			ImGui::TextDisabled("Running with CS closed");
			return;
		}

		if (state.phase != FeatureCostMeasurementPhase::Complete)
			return;

		if (!state.failureMessage.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetWarning());
			ImGui::TextWrapped("%s", state.failureMessage.c_str());
			ImGui::PopStyleColor();
			return;
		}

		if (!state.delta.frame.available && !state.delta.fps.available &&
			!state.delta.gameGpu.available && !state.delta.gameCpu.available) {
			ImGui::SameLine();
			ImGui::TextDisabled("No game timing data");
			return;
		}

		ImGui::Spacing();
		const std::string differenceHeader = fmt::format(
			"Current - {} \xC2\xB1 SE",
			GetFeatureCostComparisonLabel(feature));
		if (ImGui::BeginTable(
				"##FeatureCostResults",
				2,
				ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersInnerH |
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn(differenceHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
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
		ImGui::TextDisabled(
			"* p <= 0.05    \xE2\x80\xA0"
			"1/\xE2\x80\xA0"
			"2: raw samples missing; 3+ = --");
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
		if (!feature || feature->GetShortName() == "WetnessEffects")
			return false;

		const std::string shortName = feature->GetShortName();
		return std::ranges::find(kPerformanceFeatureOrder, std::string_view(shortName)) != kPerformanceFeatureOrder.end();
	}

	std::vector<Feature*> BuildPerformanceFeatureList()
	{
		std::vector<Feature*> features;
		const bool essentialsMode = globals::menu && globals::menu->IsEssentialsUiMode();
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

	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!IsFeatureCostMeasurementActive(state)) {
			return;
		}

		if (feature && state.testStateApplied)
			RestoreFeatureCostMeasurementOriginalState(feature, state);

		state = {};
	}

	void ClearFinishedFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		if (state.phase == FeatureCostMeasurementPhase::Complete) {
			state = {};
		}
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
				{ "enabled", feature->IsPerformanceCostMeasurementEnabled() },
				{ "ready", feature->IsPerformanceCostMeasurementReady() },
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
		}

		const bool sweepKnown = g_upscalingCostSweep.phase != UpscalingCostSweepPhase::Idle;
		const bool sweepRunning = IsUpscalingCostSweepRunning();
		const bool nvidiaSweep = sweepKnown &&
		                         g_upscalingCostSweep.matrix == UpscalingCostSweepMatrix::Nvidia;
		const auto readiness = CaptureUpscalingCostSweepReadiness(currentTime);
		const auto& fidelityFX = Upscaling::fidelityFX;
		json response = json::object();
		response["active"] = IsAnyFeatureCostMeasurementActive() || sweepRunning;
		response["owner"] = sweepRunning ?
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

void PerformanceTuningRenderer::Render()
{
	static std::string selectedShortName;
	static TuningHighlightState highlightState;

	CaptureProfilerStateForPerformanceTuning();
	SyncFeatureCostVanityCameraSuppression();

	const auto features = BuildPerformanceFeatureList();
	auto* selectedFeature = FindSelectedFeature(features, selectedShortName);
	if (!selectedFeature) {
		ImGui::TextDisabled("No loaded performance settings are available.");
		return;
	}

	const auto featurePrefixes = BuildPerformanceFeaturePrefixes(features);
	const auto timingBeforeSettings = ProfilingRenderer::CapturePerformanceTimingSummary(featurePrefixes, true);
	const int frameCount = ImGui::GetFrameCount();
	UpdateHighlightState(highlightState, timingBeforeSettings, features, frameCount);
	const bool anyMeasurementRunning = IsAnyFeatureCostMeasurementActive();

	const float selectorWidth = std::max(180.0f * Util::GetUIScale(), ImGui::GetContentRegionAvail().x * 0.18f);

	ImGui::TextWrapped("Changes appear live in the game and performance data. Actual feature cost closes CS for more precise results, then reopens it.");
	ImGui::Spacing();
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
			const json settingsStateBefore = selectedFeature->CapturePerformanceSettingsState();
			ImGui::BeginDisabled(anyMeasurementRunning);
			ImGui::BeginGroup();
			selectedFeature->DrawPerformanceSettings(true);
			ImGui::EndGroup();
			ImGui::EndDisabled();
			RenderFeatureCostMeasurement(selectedFeature, selectedCostState);
			const bool settingsRestored = RenderPerformanceUserDefaultButtons(selectedFeature, IsAnyFeatureCostMeasurementActive());
			const json settingsStateAfter = selectedFeature->CapturePerformanceSettingsState();
			const bool settingsEdited = settingsRestored || settingsStateBefore != settingsStateAfter;
			if (settingsEdited) {
				RegisterSettingsEdit(highlightState, timingBeforeSettings, frameCount);
				ClearFinishedFeatureCostMeasurement(selectedCostState);
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
			RestoreFeatureCostMeasurementOriginalState(feature, state);
			state.delta = {};
			state.failureMessage = "Measurement stopped: timing did not complete.";
			state.phase = FeatureCostMeasurementPhase::Complete;
			StartFeatureCostRestartCooldown(currentTime);
			shouldReopenMenu = shouldReopenMenu || reopenMenuOnCompletion;
			completedDevBenchMeasurement = completedDevBenchMeasurement || devBenchOwned;
			logger::warn("Actual feature cost measurement for '{}' timed out after {} seconds", shortName, kFeatureCostMaximumRunSeconds);
			continue;
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
	if (completedDevBenchMeasurement && !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning())
		RestoreProfilerStateAfterPerformanceTuning();
	if (shouldReopenMenu && !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning() &&
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
	if (!activeState && !sweepRunning)
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
	} else if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::InterCaseCooldown) {
		remainingSeconds = std::max(1.0, GetFeatureCostRestartCooldownRemaining(currentTime));
	} else if (g_upscalingCostSweep.phase == UpscalingCostSweepPhase::RestoringOriginal) {
		remainingSeconds = std::max(
			1.0,
			kFeatureCostRestoreWaitSeconds - (currentTime - g_upscalingCostSweep.phaseStartTime));
		caseProgress = 0.99f;
	}

	float progress = caseProgress;
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
		} else if (activeFeature) {
			ImGui::Text("Measuring %s", activeFeature->GetDisplayName().c_str());
		} else {
			ImGui::TextUnformatted("Measuring");
		}
		ImGui::TextColored(Util::Colors::GetWarning(), "Keep still until measurement completes.");
		const std::string progressText = fmt::format(
			"{:.0f} seconds remaining",
			std::ceil(remainingSeconds));
		ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), progressText.c_str());
	}
	ImGui::End();
}

void PerformanceTuningRenderer::CancelActiveMeasurements()
{
	const double currentTime = ImGui::GetTime();
	(void)CancelUpscalingCostSweep(currentTime);
	for (auto& [shortName, state] : g_costMeasurementStates) {
		ClearFinishedFeatureCostMeasurement(state);
		if (IsFeatureCostMeasurementActive(state))
			CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state);
	}

	SyncFeatureCostVanityCameraSuppression();
	if (!IsUpscalingCostSweepRunning())
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
	if (!startedMeasurement && !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning())
		RestoreProfilerStateAfterPerformanceTuning();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning();
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
	if (!globals::state || globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen ||
		!RE::PlayerCharacter::GetSingleton())
		return reject("not_in_game");
	if (!globals::menu)
		return reject("menu_unavailable");
	if (auto* editor = EditorWindow::GetSingleton(); editor && editor->open)
		return reject("editor_open");
	if (IsAnyFeatureCostMeasurementActive() || IsUpscalingCostSweepRunning())
		return reject("measurement_busy");
	if (GetFeatureCostRestartCooldownRemaining(currentTime) > 0.0)
		return reject("restart_cooldown");
	if (!feature->IsPerformanceCostMeasurementEnabled())
		return reject("feature_inactive");
	if (!feature->IsPerformanceCostMeasurementReady())
		return reject("feature_not_ready");

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
	bool reopenMenu = false;
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (!state.devBenchOwned || !IsFeatureCostMeasurementActive(state))
			continue;

		reopenMenu = reopenMenu || state.reopenMenuOnCompletion;
		CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state);
		cancelled = true;
	}
	SyncFeatureCostVanityCameraSuppression();
	if (!IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning())
		RestoreProfilerStateAfterPerformanceTuning();
	if (reopenMenu && !IsAnyFeatureCostMeasurementActive() && !IsUpscalingCostSweepRunning() &&
		globals::menu && !globals::menu->IsEnabled) {
		globals::menu->OpenMenu();
	}
	return {
		{ "action", "cancel" },
		{ "accepted", cancelled },
		{ "status", BuildDevBenchMeasurementStatus(0, 128) },
	};
}
