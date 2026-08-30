#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <imgui_internal.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
	constexpr double kFeatureCostMeasurementSeconds = 3.0;
	constexpr double kFeatureCostMeasurementMilliseconds = kFeatureCostMeasurementSeconds * 1000.0;
	constexpr double kFeatureCostBaselineWindowMilliseconds = 500.0;
	constexpr double kFeatureCostBaselineTimeoutSeconds = 2.25;
	constexpr double kFeatureCostBaselineMinSampleWeight = 4.0;
	constexpr double kFeatureCostBaselineMeanAbsoluteToleranceMs = 0.5;
	constexpr double kFeatureCostBaselineMeanRelativeTolerance = 0.05;
	constexpr std::size_t kFeatureCostMeasurementBlockCount =
		static_cast<std::size_t>(kFeatureCostMeasurementMilliseconds / kFeatureCostBaselineWindowMilliseconds);
	static_assert(
		kFeatureCostMeasurementBlockCount > 1 &&
		kFeatureCostMeasurementBlockCount * kFeatureCostBaselineWindowMilliseconds ==
			kFeatureCostMeasurementMilliseconds);

	bool IsRenderScaleDesktopMirrorQualityAvailable()
	{
		return globals::game::isVR && globals::features::upscaling.IsVRRenderScaleModeActive();
	}

	constexpr std::array<std::string_view, 14> kPerformanceFeatureOrder = {
		"Upscaling",
		"VR",
		"ScreenSpaceShadows",
		"ScreenSpaceGI",
		"LightLimitFix",
		"Skylighting",
		"TerrainBlending",
		"TerrainShadows",
		"VolumetricLighting",
		"UnifiedWater",
		"Wetterness",
		"SubsurfaceScattering",
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
	};

	struct FeatureCostSample
	{
		double sampledDurationMs = 0.0;
		FeatureCostMetricSample frame;
		FeatureCostMetricSample gameGpu;
		FeatureCostMetricSample gameCpu;
		uint32_t lastFrameCount = 0;
	};

	struct FeatureCostBaselineWindow
	{
		double sampledDurationMs = 0.0;
		double frameMsSum = 0.0;
		double frameSampleWeight = 0.0;
	};

	struct FeatureCostBaselineState
	{
		FeatureCostBaselineWindow previousWindow;
		FeatureCostBaselineWindow currentWindow;
		bool hasPreviousWindow = false;
		uint32_t completedWindowCount = 0;
		uint32_t lastFrameCount = 0;
		bool monitoringStarted = false;
		double monitoringStartTime = 0.0;
	};

	struct FeatureCostMetricDelta
	{
		float value = 0.0f;
		float standardError = 0.0f;
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
		SettlingCurrent,
		MeasuringCurrent,
		AwaitingMenuClose,
		AwaitingContinue,
		SettlingTest,
		MeasuringTest,
		AwaitingRestoreMenuClose,
		AwaitingRestoreContinue,
		SettlingRestoredCurrent,
		MeasuringRestoredCurrent,
		Complete
	};

	enum class FeatureCostBaselineResult
	{
		Pending,
		Stable,
		NoisyTimeout
	};

	enum class FeatureCostSampleResult
	{
		Pending,
		Complete,
		Interrupted
	};

	enum class FeatureCostPostRestoreAction
	{
		MeasureRestoredCurrent,
		Discard
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
		FeatureCostPostRestoreAction postRestoreAction = FeatureCostPostRestoreAction::MeasureRestoredCurrent;
		ULONGLONG menuCloseTick = 0;
		double phaseStartTime = 0.0;
		FeatureCostBaselineState baseline;
		bool noisyBaseline = false;
		FeatureCostSample currentSample;
		FeatureCostSample testSample;
		FeatureCostSample restoredCurrentSample;
		FeatureCostDelta delta;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
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

	bool IsFeatureCostMeasurementRunning(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::SettlingCurrent ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
		       state.phase == FeatureCostMeasurementPhase::SettlingTest ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringTest ||
		       state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent;
	}

	bool IsFeatureCostMeasurementSettling(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::SettlingCurrent ||
		       state.phase == FeatureCostMeasurementPhase::SettlingTest ||
		       state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent;
	}

	bool IsFeatureCostMeasurementPending(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose ||
		       state.phase == FeatureCostMeasurementPhase::AwaitingContinue ||
		       state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose ||
		       state.phase == FeatureCostMeasurementPhase::AwaitingRestoreContinue;
	}

	bool IsFeatureCostMeasurementActive(const FeatureCostMeasurementState& state)
	{
		return IsFeatureCostMeasurementRunning(state) || IsFeatureCostMeasurementPending(state);
	}

	bool IsAnyFeatureCostMeasurementRunning()
	{
		for (const auto& [_, state] : g_costMeasurementStates) {
			if (IsFeatureCostMeasurementActive(state))
				return true;
		}

		return false;
	}

	void SyncFeatureCostVanityCameraSuppression()
	{
		if (IsAnyFeatureCostMeasurementRunning())
			g_featureCostVanityCameraSuppression.Acquire();
		else
			g_featureCostVanityCameraSuppression.Release();
	}

	double GetFeatureCostMean(const FeatureCostMetricSample& sample)
	{
		return PerformanceTuningStatistics::GetMean(
			PerformanceTuningStatistics::CombineMoments(sample.blocks));
	}

	double GetFeatureCostSampleWeight(const FeatureCostMetricSample& sample)
	{
		return PerformanceTuningStatistics::CombineMoments(sample.blocks).sampleWeight;
	}

	bool IsPositiveFiniteTiming(float value)
	{
		return std::isfinite(value) && value > 0.0f;
	}

	bool IsValidFeatureCostTiming(float value)
	{
		return PerformanceTuningStatistics::IsValidTiming(static_cast<double>(value));
	}

	void AddFeatureCostMoment(
		FeatureCostMetricSample& sample,
		std::size_t blockIndex,
		float value,
		double sampleWeight)
	{
		if (blockIndex >= sample.blocks.size())
			return;

		PerformanceTuningStatistics::AddMoment(sample.blocks[blockIndex], value, sampleWeight);
	}

	bool TryGetFeatureCostMeanVariance(const FeatureCostMetricSample& sample, double& meanVariance)
	{
		return PerformanceTuningStatistics::TryGetBlockMeanVariance(sample.blocks, meanVariance);
	}

	void SetFeatureCostSignificance(FeatureCostMetricDelta& delta, double standardError)
	{
		const auto significance = PerformanceTuningStatistics::EvaluateSignificance(delta.value, standardError);
		if (!significance.hasStandardError)
			return;

		delta.standardError = static_cast<float>(significance.standardError);
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
		const FeatureCostMetricSample& currentBefore,
		const FeatureCostMetricSample& comparison,
		const FeatureCostMetricSample& currentAfter,
		bool available)
	{
		FeatureCostMetricAnalysis analysis;
		analysis.delta.available = available &&
		                           GetFeatureCostSampleWeight(currentBefore) > 0.0 &&
		                           GetFeatureCostSampleWeight(comparison) > 0.0 &&
		                           GetFeatureCostSampleWeight(currentAfter) > 0.0;
		if (!analysis.delta.available)
			return analysis;

		const double currentBeforeMean = GetFeatureCostMean(currentBefore);
		const double currentAfterMean = GetFeatureCostMean(currentAfter);
		analysis.currentMean = (currentBeforeMean + currentAfterMean) * 0.5;
		analysis.comparisonMean = GetFeatureCostMean(comparison);
		analysis.delta.value = static_cast<float>(analysis.currentMean - analysis.comparisonMean);

		double currentBeforeMeanVariance = 0.0;
		double currentAfterMeanVariance = 0.0;
		if (!TryGetFeatureCostMeanVariance(currentBefore, currentBeforeMeanVariance) ||
			!TryGetFeatureCostMeanVariance(currentAfter, currentAfterMeanVariance) ||
			!TryGetFeatureCostMeanVariance(comparison, analysis.comparisonMeanVariance)) {
			return analysis;
		}

		const double currentSamplingVariance =
			(currentBeforeMeanVariance + currentAfterMeanVariance) * 0.25;
		const double currentDriftVariance =
			(currentBeforeMean - currentAfterMean) *
			(currentBeforeMean - currentAfterMean) * 0.25;
		// The A/B/A estimate must not claim more certainty than the observed
		// separation between its two equally weighted current-state windows.
		analysis.currentMeanVariance = std::max(currentSamplingVariance, currentDriftVariance);
		SetFeatureCostSignificance(
			analysis.delta,
			std::sqrt(analysis.currentMeanVariance + analysis.comparisonMeanVariance));
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
		return fps;
	}

	double GetFeatureCostBaselineMean(const FeatureCostBaselineWindow& window)
	{
		return window.frameSampleWeight > 0.0 ? window.frameMsSum / window.frameSampleWeight : 0.0;
	}

	bool AreFeatureCostBaselineWindowsClose(
		const FeatureCostBaselineWindow& previous,
		const FeatureCostBaselineWindow& current)
	{
		if (previous.frameSampleWeight < kFeatureCostBaselineMinSampleWeight ||
			current.frameSampleWeight < kFeatureCostBaselineMinSampleWeight) {
			return false;
		}

		const double previousMean = GetFeatureCostBaselineMean(previous);
		const double currentMean = GetFeatureCostBaselineMean(current);
		const double referenceMean = (previousMean + currentMean) * 0.5;
		const double tolerance = std::max(
			kFeatureCostBaselineMeanAbsoluteToleranceMs,
			referenceMean * kFeatureCostBaselineMeanRelativeTolerance);
		return std::abs(previousMean - currentMean) <= tolerance;
	}

	bool CompleteFeatureCostBaselineWindow(FeatureCostBaselineState& baseline)
	{
		baseline.completedWindowCount++;
		if (!baseline.hasPreviousWindow) {
			baseline.previousWindow = baseline.currentWindow;
			baseline.currentWindow = {};
			baseline.hasPreviousWindow = true;
			return false;
		}

		const bool averagesClose =
			AreFeatureCostBaselineWindowsClose(baseline.previousWindow, baseline.currentWindow);
		baseline.previousWindow = baseline.currentWindow;
		baseline.currentWindow = {};
		return averagesClose;
	}

	void StartFeatureCostBaselineMonitoring(
		FeatureCostBaselineState& baseline,
		uint32_t frameCount,
		double currentTime)
	{
		baseline = {};
		baseline.monitoringStarted = true;
		baseline.monitoringStartTime = currentTime;
		baseline.lastFrameCount = frameCount;
	}

	void ResetFeatureCostBaselineEvidence(
		FeatureCostBaselineState& baseline,
		uint32_t frameCount)
	{
		baseline.previousWindow = {};
		baseline.currentWindow = {};
		baseline.hasPreviousWindow = false;
		baseline.completedWindowCount = 0;
		baseline.lastFrameCount = frameCount;
	}

	FeatureCostBaselineResult UpdateFeatureCostBaseline(
		FeatureCostBaselineState& baseline,
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		double currentTime)
	{
		if (!baseline.monitoringStarted) {
			StartFeatureCostBaselineMonitoring(baseline, summary.frameCount, currentTime);
			return FeatureCostBaselineResult::Pending;
		}

		if (summary.frameCount == 0) {
			if (PerformanceTuningStatistics::IsTimingSampleInterrupted(
					baseline.lastFrameCount,
					summary.frameCount,
					false)) {
				ResetFeatureCostBaselineEvidence(baseline, summary.frameCount);
			}
		} else if (summary.frameCount != baseline.lastFrameCount) {
			const bool validTiming =
				summary.valid &&
				summary.hasFrameSample &&
				IsValidFeatureCostTiming(summary.frameSampleMs);
			if (PerformanceTuningStatistics::IsTimingSampleInterrupted(
					baseline.lastFrameCount,
					summary.frameCount,
					validTiming)) {
				// Interruptions invalidate adjacency but do not extend the bounded wait.
				ResetFeatureCostBaselineEvidence(baseline, summary.frameCount);
			} else {
				baseline.lastFrameCount = summary.frameCount;
			}

			if (validTiming) {
				const double frameMs = static_cast<double>(summary.frameSampleMs);
				double remainingSampleWeight = 1.0;
				while (remainingSampleWeight > 1.0e-9) {
					auto& window = baseline.currentWindow;
					const double remainingWindowMs =
						kFeatureCostBaselineWindowMilliseconds - window.sampledDurationMs;
					const double sampleWeight =
						std::min(remainingSampleWeight, remainingWindowMs / frameMs);
					if (sampleWeight <= 0.0)
						break;

					window.sampledDurationMs = std::min(
						kFeatureCostBaselineWindowMilliseconds,
						window.sampledDurationMs + frameMs * sampleWeight);
					window.frameMsSum += frameMs * sampleWeight;
					window.frameSampleWeight += sampleWeight;
					remainingSampleWeight -= sampleWeight;

					if (window.sampledDurationMs >= kFeatureCostBaselineWindowMilliseconds &&
						CompleteFeatureCostBaselineWindow(baseline)) {
						return FeatureCostBaselineResult::Stable;
					}
				}
			}
		}

		if (currentTime - baseline.monitoringStartTime >= kFeatureCostBaselineTimeoutSeconds)
			return FeatureCostBaselineResult::NoisyTimeout;

		return FeatureCostBaselineResult::Pending;
	}

	bool HasFeatureCostMetricSamples(
		const FeatureCostSample& sample,
		const FeatureCostMetricSample& metric)
	{
		return GetFeatureCostSampleWeight(sample.frame) > 0.0 &&
		       GetFeatureCostSampleWeight(metric) > 0.0;
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
		       shortName == "ScreenSpaceGI";
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
		double remainingSampleWeight = std::min(1.0, remainingDurationMs / frameMs);
		while (remainingSampleWeight > 1.0e-9) {
			const auto blockIndex = std::min(
				static_cast<std::size_t>(sample.sampledDurationMs / kFeatureCostBaselineWindowMilliseconds),
				kFeatureCostMeasurementBlockCount - 1);
			const double blockEndMs =
				static_cast<double>(blockIndex + 1) * kFeatureCostBaselineWindowMilliseconds;
			const double remainingBlockMs = blockEndMs - sample.sampledDurationMs;
			const double availableFrameMs = frameMs * remainingSampleWeight;
			const bool completesBlock = availableFrameMs >= remainingBlockMs;
			const double chunkDurationMs = std::min(availableFrameMs, remainingBlockMs);
			const double chunkWeight = chunkDurationMs / frameMs;
			if (chunkWeight <= 0.0)
				break;

			AddFeatureCostMoment(sample.frame, blockIndex, summary.frameSampleMs, chunkWeight);
			if (summary.hasGameGpuSample)
				AddFeatureCostMoment(sample.gameGpu, blockIndex, summary.gameGpuSampleMs, chunkWeight);
			if (summary.hasGameCpuSample)
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
		const bool hasFrame =
			GetFeatureCostSampleWeight(state.currentSample.frame) > 0.0 &&
			GetFeatureCostSampleWeight(state.restoredCurrentSample.frame) > 0.0 &&
			GetFeatureCostSampleWeight(state.testSample.frame) > 0.0;
		const bool hasGameGpu =
			HasFeatureCostMetricSamples(state.currentSample, state.currentSample.gameGpu) &&
			HasFeatureCostMetricSamples(state.restoredCurrentSample, state.restoredCurrentSample.gameGpu) &&
			HasFeatureCostMetricSamples(state.testSample, state.testSample.gameGpu);
		const bool hasGameCpu =
			HasFeatureCostMetricSamples(state.currentSample, state.currentSample.gameCpu) &&
			HasFeatureCostMetricSamples(state.restoredCurrentSample, state.restoredCurrentSample.gameCpu) &&
			HasFeatureCostMetricSamples(state.testSample, state.testSample.gameCpu);

		const auto frame = AnalyzeFeatureCostMetric(
			state.currentSample.frame,
			state.testSample.frame,
			state.restoredCurrentSample.frame,
			hasFrame);
		state.delta.frame = frame.delta;
		state.delta.fps = AnalyzeFeatureCostFps(frame);
		const auto gameGpu = AnalyzeFeatureCostMetric(
			state.currentSample.gameGpu,
			state.testSample.gameGpu,
			state.restoredCurrentSample.gameGpu,
			hasGameGpu);
		const auto gameCpu = AnalyzeFeatureCostMetric(
			state.currentSample.gameCpu,
			state.testSample.gameCpu,
			state.restoredCurrentSample.gameCpu,
			hasGameCpu);
		state.delta.gameGpu = gameGpu.delta;
		state.delta.gameCpu = gameCpu.delta;
	}

	void PrepareFeatureCostMeasurementSettle(
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		sample = {};
		state.baseline = {};
		state.phase = phase;
		state.phaseStartTime = currentTime;
	}

	void StartFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (!feature || !feature->SupportsPerformanceCostMeasurement() || !feature->IsPerformanceCostMeasurementEnabled())
			return;
		if (!g_featureCostVanityCameraSuppression.Acquire()) {
			logger::error("Actual feature cost measurement was not started because the automatic vanity camera could not be suppressed");
			return;
		}

		state = {};
		state.originalState = feature->CapturePerformanceCostMeasurementState();
		state.testEnabled = false;
		PrepareFeatureCostMeasurementSettle(
			state.currentSample,
			FeatureCostMeasurementPhase::SettlingCurrent,
			state,
			currentTime);
	}

	void ApplyFeatureCostMeasurementTestState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature)
			return;

		feature->SetPerformanceCostMeasurementEnabled(state.testEnabled);
		state.testStateApplied = true;
	}

	void PrepareFeatureCostMeasurementTestSettle(FeatureCostMeasurementState& state, double currentTime)
	{
		PrepareFeatureCostMeasurementSettle(
			state.testSample,
			FeatureCostMeasurementPhase::SettlingTest,
			state,
			currentTime);
	}

	void BeginFeatureCostMeasurementTestSettle(Feature* feature, FeatureCostMeasurementState& state, double currentTime)
	{
		ApplyFeatureCostMeasurementTestState(feature, state);
		PrepareFeatureCostMeasurementTestSettle(state, currentTime);
	}

	void RestoreFeatureCostMeasurementOriginalState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature || !state.testStateApplied)
			return;

		feature->RestorePerformanceCostMeasurementState(state.originalState);
		state.testStateApplied = false;
	}

	void PrepareFeatureCostMeasurementRestoredCurrentSettle(FeatureCostMeasurementState& state, double currentTime)
	{
		PrepareFeatureCostMeasurementSettle(
			state.restoredCurrentSample,
			FeatureCostMeasurementPhase::SettlingRestoredCurrent,
			state,
			currentTime);
	}

	void CompleteFeatureCostPostRestoreAction(FeatureCostMeasurementState& state, double currentTime)
	{
		switch (state.postRestoreAction) {
		case FeatureCostPostRestoreAction::MeasureRestoredCurrent:
			PrepareFeatureCostMeasurementRestoredCurrentSettle(state, currentTime);
			break;
		case FeatureCostPostRestoreAction::Discard:
			state = {};
			break;
		}
	}

	void CompleteFeatureCostPostRestoreActionAfterMenuClose(
		FeatureCostMeasurementState& state,
		ULONGLONG menuCloseTick)
	{
		if (state.postRestoreAction == FeatureCostPostRestoreAction::MeasureRestoredCurrent) {
			state.baseline = {};
			state.phase = FeatureCostMeasurementPhase::AwaitingRestoreContinue;
			state.menuCloseTick = menuCloseTick;
			return;
		}

		CompleteFeatureCostPostRestoreAction(state, 0.0);
	}

	void QueueFeatureCostMeasurementRestore(
		Feature* feature,
		FeatureCostMeasurementState& state,
		FeatureCostPostRestoreAction action,
		double currentTime)
	{
		state.postRestoreAction = action;
		state.baseline = {};
		if (state.testStateApplied && feature && feature->RequiresMenuCloseForPerformanceCostMeasurementRestore(state.originalState)) {
			state.phase = FeatureCostMeasurementPhase::AwaitingRestoreMenuClose;
			state.menuCloseTick = 0;
			return;
		}

		RestoreFeatureCostMeasurementOriginalState(feature, state);
		CompleteFeatureCostPostRestoreAction(state, currentTime);
	}

	void BeginFeatureCostSampleWindow(
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase phase,
		FeatureCostMeasurementState& state,
		const ProfilingRenderer::PerformanceTimingSummary& current,
		double currentTime)
	{
		sample = {};
		// The sample which completed the baseline check belongs to preparation.
		// Start the exact measurement window on the next VR frame.
		sample.lastFrameCount = current.frameCount;
		state.baseline = {};
		state.phase = phase;
		state.phaseStartTime = currentTime;
	}

	bool RestartInterruptedFeatureCostSample(
		FeatureCostSampleResult result,
		FeatureCostSample& sample,
		FeatureCostMeasurementPhase settlingPhase,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (result != FeatureCostSampleResult::Interrupted)
			return false;

		PrepareFeatureCostMeasurementSettle(
			sample,
			settlingPhase,
			state,
			currentTime);
		return true;
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
			state.baseline = {};
			return;
		}

		const double elapsed = currentTime - state.phaseStartTime;
		if (IsFeatureCostMeasurementSettling(state)) {
			const bool targetEnabled = state.phase != FeatureCostMeasurementPhase::SettlingTest || state.testEnabled;
			const double settleSeconds = std::max(0.0, feature->GetPerformanceCostMeasurementSettleSeconds(targetEnabled));
			if (elapsed < settleSeconds)
				return;

			const auto baselineResult = UpdateFeatureCostBaseline(state.baseline, current, currentTime);
			if (baselineResult == FeatureCostBaselineResult::Pending)
				return;
			if (baselineResult == FeatureCostBaselineResult::NoisyTimeout)
				state.noisyBaseline = true;

			if (state.phase == FeatureCostMeasurementPhase::SettlingCurrent) {
				BeginFeatureCostSampleWindow(
					state.currentSample,
					FeatureCostMeasurementPhase::MeasuringCurrent,
					state,
					current,
					currentTime);
			} else if (state.phase == FeatureCostMeasurementPhase::SettlingTest) {
				BeginFeatureCostSampleWindow(
					state.testSample,
					FeatureCostMeasurementPhase::MeasuringTest,
					state,
					current,
					currentTime);
			} else {
				BeginFeatureCostSampleWindow(
					state.restoredCurrentSample,
					FeatureCostMeasurementPhase::MeasuringRestoredCurrent,
					state,
					current,
					currentTime);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
			const auto sampleResult = AddFeatureCostSample(state.currentSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.currentSample,
					FeatureCostMeasurementPhase::SettlingCurrent,
					state,
					currentTime)) {
				return;
			}
			if (sampleResult == FeatureCostSampleResult::Complete) {
				if (feature->RequiresMenuCloseForPerformanceCostMeasurement(state.testEnabled)) {
					state.phase = FeatureCostMeasurementPhase::AwaitingMenuClose;
					state.menuCloseTick = 0;
					state.baseline = {};
				} else {
					BeginFeatureCostMeasurementTestSettle(feature, state, currentTime);
				}
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
			const auto sampleResult = AddFeatureCostSample(state.testSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.testSample,
					FeatureCostMeasurementPhase::SettlingTest,
					state,
					currentTime)) {
				return;
			}
			if (sampleResult == FeatureCostSampleResult::Complete) {
				QueueFeatureCostMeasurementRestore(
					feature,
					state,
					FeatureCostPostRestoreAction::MeasureRestoredCurrent,
					currentTime);
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringRestoredCurrent) {
			const auto sampleResult = AddFeatureCostSample(state.restoredCurrentSample, current);
			if (RestartInterruptedFeatureCostSample(
					sampleResult,
					state.restoredCurrentSample,
					FeatureCostMeasurementPhase::SettlingRestoredCurrent,
					state,
					currentTime)) {
				return;
			}
			if (sampleResult == FeatureCostSampleResult::Complete) {
				FinalizeFeatureCostMeasurement(state);
				state.phase = FeatureCostMeasurementPhase::Complete;
			}
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
		ImGui::TextDisabled("%s", label);
		ImGui::TableSetColumnIndex(1);
		if (!metric.available) {
			ImGui::TextDisabled("--");
			return;
		}

		const int colorDirection = metric.significant ? direction : 0;
		if (colorDirection != 0)
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::PerformanceDelta(colorDirection));

		const char* significanceMarker = metric.significant ? "*" : "";
		if (metric.hasStandardError) {
			if (fps) {
				ImGui::Text("%+.1f%s \xC2\xB1 %.1f", metric.value, significanceMarker, metric.standardError);
			} else {
				ImGui::Text("%+.3f%s \xC2\xB1 %.3f ms", metric.value, significanceMarker, metric.standardError);
			}
		} else if (fps) {
			ImGui::Text("%+.1f (SE --)", metric.value);
		} else {
			ImGui::Text("%+.3f ms (SE --)", metric.value);
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
			return "Upscaling is set to None, with foveated upscaling disabled.";
		if (shortName == "VR")
			return "depth culling, screen-space stereo sync, screen-space FOV, stereo blend, shader FOV, and dynamic cubemap throttle are switched off.";
		if (shortName == "ScreenSpaceShadows")
			return "Screen Space Shadows are switched off.";
		if (shortName == "ScreenSpaceGI")
			return "SSGI/AO is switched off.";
		if (shortName == "LightLimitFix")
			return "particle lights, point-light contact shadows, and particle contact shadows are switched off.";
		if (shortName == "Skylighting")
			return "Skylighting's in-game Enable toggle is switched off, so probe updates stop and ambient shading plus reflection occlusion fall back to the unoccluded path.";
		if (shortName == "TerrainBlending")
			return "Terrain Blending is switched off.";
		if (shortName == "TerrainShadows")
			return "Terrain Shadows are switched off.";
		if (shortName == "VolumetricLighting")
			return "Volumetric Lighting is switched off for the current interior/exterior context.";
		if (shortName == "UnifiedWater")
			return "optimized water meshes are switched off.";
		if (shortName == "Wetterness")
			return "Wetterness is switched off.";
		if (shortName == "SubsurfaceScattering")
			return "Subsurface Scattering is switched off.";
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
				"Waits for each state to become ready and complete its feature-specific settle delay, measures current settings for %.0f seconds, measures the comparison state for %.0f seconds, restores the original settings, then measures current once more.",
				kFeatureCostMeasurementSeconds,
				kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped("Each window accumulates exactly %.0f seconds of the existing VR game-frame timing source; only the final boundary frame is fractionally weighted.", kFeatureCostMeasurementSeconds);
			ImGui::TextWrapped("Before every measurement, adjacent 0.5-second average frame times are compared. A close pair starts capture early; after %.2f seconds, capture proceeds with a noisy-baseline warning.", kFeatureCostBaselineTimeoutSeconds);
			ImGui::TextWrapped("If game-frame timing is interrupted during capture, only that three-second leg repeats its settle and bounded baseline preparation.");
			ImGui::TextWrapped("GPU and CPU averages use only valid positive samples. Missing and zero samples are excluded; each row only requires valid samples in every A/B/A window and never blocks Game or FPS.");
			ImGui::TextWrapped("Results are mean differences plus or minus standard error. Uncertainty is estimated from non-overlapping 0.5-second block means so consecutive frames are not counted as independent evidence.");
			ImGui::TextWrapped("An asterisk after a value marks a two-sided block-mean normal test with p <= 0.05; percentile rows are not included.");
			ImGui::TextWrapped("The automatic idle/vanity camera remains suppressed for the complete run and its previous delay is restored afterward.");
			if (feature && feature->GetShortName() == "Skylighting") {
				ImGui::TextWrapped("For Skylighting, the comparison state is its in-game Enable toggle set to Off, not a lower preset.");
			}
			ImGui::TextWrapped(
				"Comparison: %s - %s",
				GetFeatureCostComparisonLabel(feature, state),
				GetFeatureCostComparisonDetails(feature));
		}
		if (!running && anyMeasurementRunning && !IsFeatureCostMeasurementActive(state)) {
			ImGui::SameLine();
			ImGui::TextDisabled("Finish the current measurement first");
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose) {
			ImGui::Spacing();
			ImGui::TextDisabled("Close the CSX menu now.");
			ImGui::TextWrapped(
				"Wait at least %.0f seconds with the menu closed so the comparison state can apply, then reopen and continue.",
				feature->GetPerformanceCostMeasurementMenuCloseWaitMs() / 1000.0f);
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingContinue ||
			state.phase == FeatureCostMeasurementPhase::AwaitingRestoreContinue) {
			const bool restoringCurrent = state.phase == FeatureCostMeasurementPhase::AwaitingRestoreContinue;
			const char* measurementLabel = restoringCurrent ?
			                                   "Continue restored-current measurement" :
			                                   "Continue comparison measurement";
			const ULONGLONG waitMs = feature->GetPerformanceCostMeasurementMenuCloseWaitMs();
			const ULONGLONG elapsedCloseMs = state.menuCloseTick > 0 ? GetTickCount64() - state.menuCloseTick : 0;
			const bool closeWaitSatisfied = state.menuCloseTick > 0 && elapsedCloseMs >= waitMs;
			const bool readyToContinue = closeWaitSatisfied && feature->IsPerformanceCostMeasurementReady();

			ImGui::Spacing();
			if (state.menuCloseTick == 0) {
				ImGui::TextDisabled("Close the CSX menu and wait %.0f seconds.", waitMs / 1000.0f);
			} else if (!closeWaitSatisfied) {
				const double remainingSeconds = static_cast<double>(waitMs - elapsedCloseMs) / 1000.0;
				ImGui::TextDisabled("Wait %.1f more seconds with the menu closed, then reopen.", remainingSeconds);
			} else if (!feature->IsPerformanceCostMeasurementReady()) {
				ImGui::TextDisabled("%s", feature->GetPerformanceCostMeasurementWaitText());
			} else {
				ImGui::TextDisabled(
					"Ready to begin %s.",
					restoringCurrent ? "the final current measurement" : "the comparison measurement");
			}

			ImGui::BeginDisabled(!readyToContinue);
			if (ImGui::Button(measurementLabel)) {
				if (restoringCurrent)
					PrepareFeatureCostMeasurementRestoredCurrentSettle(state, ImGui::GetTime());
				else
					PrepareFeatureCostMeasurementTestSettle(state, ImGui::GetTime());
			}
			ImGui::EndDisabled();
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose) {
			ImGui::Spacing();
			if (state.postRestoreAction == FeatureCostPostRestoreAction::Discard)
				ImGui::TextDisabled("The measurement was cancelled.");
			else
				ImGui::TextDisabled("The comparison measurement is done.");
			ImGui::TextWrapped("Close the CSX menu now so your previous settings can be restored safely.");
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::SettlingCurrent ||
			state.phase == FeatureCostMeasurementPhase::SettlingTest ||
			state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent) {
			const bool preparingCurrent = state.phase == FeatureCostMeasurementPhase::SettlingCurrent;
			const bool restoringCurrent = state.phase == FeatureCostMeasurementPhase::SettlingRestoredCurrent;
			const bool targetEnabled = preparingCurrent || restoringCurrent ? true : state.testEnabled;
			const double settleSeconds = std::max(0.0, feature->GetPerformanceCostMeasurementSettleSeconds(targetEnabled));
			const double elapsed = std::clamp(ImGui::GetTime() - state.phaseStartTime, 0.0, settleSeconds);
			ImGui::SameLine();
			if (state.baseline.monitoringStarted) {
				const double baselineElapsed = std::clamp(
					ImGui::GetTime() - state.baseline.monitoringStartTime,
					0.0,
					kFeatureCostBaselineTimeoutSeconds);
				const char* phaseLabel = GetFeatureCostComparisonLabel(feature, state);
				if (preparingCurrent)
					phaseLabel = "current";
				else if (restoringCurrent)
					phaseLabel = "restored current";
				ImGui::TextDisabled(
					"Checking %s averages: %u window(s) (%.1f / %.2fs)",
					phaseLabel,
					state.baseline.completedWindowCount,
					baselineElapsed,
					kFeatureCostBaselineTimeoutSeconds);
			} else if (preparingCurrent) {
				ImGui::TextDisabled("Preparing current: %s %.1f / %.1fs", feature->GetPerformanceCostMeasurementWaitText(), elapsed, settleSeconds);
			} else if (restoringCurrent) {
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
				state.phase == FeatureCostMeasurementPhase::MeasuringTest    ? state.testSample :
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

		if (!state.delta.frame.available && !state.delta.fps.available &&
			!state.delta.gameGpu.available && !state.delta.gameCpu.available) {
			ImGui::SameLine();
			ImGui::TextDisabled("No game timing data");
			return;
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Difference vs %s", GetFeatureCostComparisonLabel(feature, state));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Difference is average(current 1, current 2) minus the comparison. Standard error comes from non-overlapping 0.5-second block means and includes the larger of sampling error and the separation between the two current windows.");
			ImGui::TextWrapped("Significant frame-time increases are positive and magenta; significant decreases are negative and green. Non-significant values keep their measured sign and use normal white text. FPS uses its natural direction: gains are green and losses are magenta.");
		}
		if (ImGui::BeginTable(
				"##FeatureCostResults",
				2,
				ImGuiTableFlags_RowBg |
					ImGuiTableFlags_BordersInnerH |
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Difference \xC2\xB1 SE", ImGuiTableColumnFlags_WidthStretch);
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
		ImGui::TextDisabled("* p <= 0.05");
		if (state.noisyBaseline) {
			ImGui::TextColored(Util::Colors::GetWarning(), "Noisy baseline: capture used the bounded timeout");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped("Adjacent frame-time averages did not converge during at least one preparation period. The result still uses the complete three-second A/B/A measurement windows.");
			}
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

	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state, bool allowPendingRestore)
	{
		if (!IsFeatureCostMeasurementActive(state)) {
			return;
		}

		if (feature && state.testStateApplied) {
			if (allowPendingRestore) {
				QueueFeatureCostMeasurementRestore(
					feature,
					state,
					FeatureCostPostRestoreAction::Discard,
					ImGui::GetTime());
				return;
			}

			RestoreFeatureCostMeasurementOriginalState(feature, state);
		}

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
	const double currentTime = ImGui::GetTime();
	UpdateHighlightState(highlightState, timingBeforeSettings, features, frameCount);
	for (auto* feature : features) {
		if (!feature)
			continue;

		UpdateFeatureCostMeasurement(feature, g_costMeasurementStates[feature->GetShortName()], timingBeforeSettings, currentTime);
	}
	SyncFeatureCostVanityCameraSuppression();
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
			const json settingsStateBefore = selectedFeature->CapturePerformanceSettingsState();
			ImGui::BeginDisabled(anyMeasurementRunning);
			ImGui::BeginGroup();
			selectedFeature->DrawPerformanceSettings(true);
			ImGui::EndGroup();
			ImGui::EndDisabled();
			RenderFeatureCostMeasurement(selectedFeature, selectedCostState);
			const bool settingsRestored = RenderPerformanceUserDefaultButtons(selectedFeature, IsAnyFeatureCostMeasurementRunning());
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

void PerformanceTuningRenderer::CancelActiveMeasurements(bool includePending)
{
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (includePending)
			ClearFinishedFeatureCostMeasurement(state);

		if (!(IsFeatureCostMeasurementRunning(state) || (includePending && IsFeatureCostMeasurementPending(state)))) {
			continue;
		}

		CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state, includePending);
	}

	SyncFeatureCostVanityCameraSuppression();
	RestoreProfilerStateAfterPerformanceTuning();
}

void PerformanceTuningRenderer::NotifyMenuClosed()
{
	const ULONGLONG now = GetTickCount64();
	for (auto& [shortName, state] : g_costMeasurementStates) {
		if (state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose) {
			auto* feature = FindFeatureByShortName(shortName);
			if (!feature) {
				state = {};
				continue;
			}

			ApplyFeatureCostMeasurementTestState(feature, state);
			state.baseline = {};
			state.phase = FeatureCostMeasurementPhase::AwaitingContinue;
			state.menuCloseTick = now;
		} else if (state.phase == FeatureCostMeasurementPhase::AwaitingContinue ||
				   state.phase == FeatureCostMeasurementPhase::AwaitingRestoreContinue) {
			state.menuCloseTick = now;
		} else if (state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose) {
			auto* feature = FindFeatureByShortName(shortName);
			if (!feature) {
				state = {};
				continue;
			}

			RestoreFeatureCostMeasurementOriginalState(feature, state);
			CompleteFeatureCostPostRestoreActionAfterMenuClose(state, now);
		}
	}
	SyncFeatureCostVanityCameraSuppression();
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return IsAnyFeatureCostMeasurementRunning();
}
