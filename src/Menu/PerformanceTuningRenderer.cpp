#include "PerformanceTuningRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ProfilingRenderer.h"
#include "Profiler.h"
#include "Utils/UI.h"

namespace
{
	constexpr float kTuningDeltaThresholdMs = 0.099f;
	constexpr int kTuningSettleFrames = 20;
	constexpr int kTuningHighlightFrames = 240;
	constexpr double kFeatureCostMeasurementSeconds = 5.0;
	constexpr float kFeatureCostDisplayEpsilonMs = 0.0005f;

	constexpr std::array<std::string_view, 15> kPerformanceFeatureOrder = {
		"Upscaling",
		"VR",
		"ScreenSpaceShadows",
		"ScreenSpaceGI",
		"LightLimitFix",
		"DynamicCubemaps",
		"Skylighting",
		"TerrainBlending",
		"TerrainShadows",
		"VolumetricLighting",
		"UnifiedWater",
		"Wetterness",
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
		float frameMsSum = 0.0f;
		float fpsSum = 0.0f;
		float gameGpuMsSum = 0.0f;
		float gameCpuMsSum = 0.0f;
		int frameSamples = 0;
		int fpsSamples = 0;
		int gameGpuSamples = 0;
		int gameCpuSamples = 0;
	};

	struct FeatureCostDelta
	{
		float frameMs = 0.0f;
		float fpsCost = 0.0f;
		float gameGpuMs = 0.0f;
		float gameCpuMs = 0.0f;
		bool hasFrame = false;
		bool hasFps = false;
		bool hasGameGpu = false;
		bool hasGameCpu = false;
	};

	enum class FeatureCostMeasurementPhase
	{
		Idle,
		MeasuringCurrent,
		AwaitingMenuClose,
		AwaitingContinue,
		MeasuringTest,
		AwaitingRestoreMenuClose,
		Complete
	};

	struct FeatureCostMeasurementState
	{
		FeatureCostMeasurementPhase phase = FeatureCostMeasurementPhase::Idle;
		json originalState;
		bool testEnabled = false;
		bool testStateApplied = false;
		bool discardAfterRestore = false;
		ULONGLONG menuCloseTick = 0;
		double phaseStartTime = 0.0;
		FeatureCostSample currentSample;
		FeatureCostSample testSample;
		FeatureCostDelta delta;
	};

	static std::unordered_map<std::string, FeatureCostMeasurementState> g_costMeasurementStates;
	static bool g_profilerStateCaptured = false;
	static bool g_profilerWasUserEnabled = false;

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
		if (std::abs(deltaMs) <= kFeatureCostDisplayEpsilonMs)
			return 0;

		return deltaMs > 0.0f ? 1 : -1;
	}

	bool IsFeatureCostMeasurementRunning(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
		       state.phase == FeatureCostMeasurementPhase::MeasuringTest;
	}

	bool IsFeatureCostMeasurementPending(const FeatureCostMeasurementState& state)
	{
		return state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose ||
		       state.phase == FeatureCostMeasurementPhase::AwaitingContinue ||
		       state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose;
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

	float AverageOrZero(float sum, int count)
	{
		return count > 0 ? sum / static_cast<float>(count) : 0.0f;
	}

	ProfilingRenderer::PerformanceTimingTotals GetTimingTotalsForFeature(
		const ProfilingRenderer::PerformanceTimingSummary& summary,
		const std::string& shortName);
	std::vector<std::string> BuildProfilingPrefixesForFeature(const std::string& shortName);

	void AddFeatureCostSample(
		FeatureCostSample& sample,
		const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		if (!summary.valid)
			return;

		if (summary.frameMs > 0.0f) {
			sample.frameMsSum += summary.frameMs;
			sample.frameSamples++;
		}
		if (summary.fps > 0.0f) {
			sample.fpsSum += summary.fps;
			sample.fpsSamples++;
		}
		if (summary.hasGameGpu) {
			sample.gameGpuMsSum += summary.gameGpuMs;
			sample.gameGpuSamples++;
		}
		if (summary.hasGameCpu) {
			sample.gameCpuMsSum += summary.gameCpuMs;
			sample.gameCpuSamples++;
		}
	}

	void FinalizeFeatureCostMeasurement(FeatureCostMeasurementState& state)
	{
		const float currentFrameMs = AverageOrZero(state.currentSample.frameMsSum, state.currentSample.frameSamples);
		const float currentGameGpuMs = AverageOrZero(state.currentSample.gameGpuMsSum, state.currentSample.gameGpuSamples);
		const float currentGameCpuMs = AverageOrZero(state.currentSample.gameCpuMsSum, state.currentSample.gameCpuSamples);
		const float testFrameMs = AverageOrZero(state.testSample.frameMsSum, state.testSample.frameSamples);
		const float testGameGpuMs = AverageOrZero(state.testSample.gameGpuMsSum, state.testSample.gameGpuSamples);
		const float testGameCpuMs = AverageOrZero(state.testSample.gameCpuMsSum, state.testSample.gameCpuSamples);
		const bool hasFrame = state.currentSample.frameSamples > 0 && state.testSample.frameSamples > 0;
		const bool hasFps = hasFrame || (state.currentSample.fpsSamples > 0 && state.testSample.fpsSamples > 0);
		const float currentFps = hasFrame && currentFrameMs > 0.0f ?
		                             1000.0f / currentFrameMs :
		                             AverageOrZero(state.currentSample.fpsSum, state.currentSample.fpsSamples);
		const float testFps = hasFrame && testFrameMs > 0.0f ?
		                          1000.0f / testFrameMs :
		                          AverageOrZero(state.testSample.fpsSum, state.testSample.fpsSamples);

		state.delta.frameMs = currentFrameMs - testFrameMs;
		state.delta.fpsCost = testFps - currentFps;
		state.delta.gameGpuMs = currentGameGpuMs - testGameGpuMs;
		state.delta.gameCpuMs = currentGameCpuMs - testGameCpuMs;
		state.delta.hasFrame = hasFrame;
		state.delta.hasFps = hasFps;
		state.delta.hasGameGpu = state.currentSample.gameGpuSamples > 0 && state.testSample.gameGpuSamples > 0;
		state.delta.hasGameCpu = state.currentSample.gameCpuSamples > 0 && state.testSample.gameCpuSamples > 0;
	}

	void StartFeatureCostMeasurement(
		Feature* feature,
		FeatureCostMeasurementState& state,
		double currentTime)
	{
		if (!feature || !feature->SupportsPerformanceCostMeasurement())
			return;

		state = {};
		state.originalState = feature->CapturePerformanceCostMeasurementState();
		if (!feature->IsPerformanceCostMeasurementEnabled())
			return;

		state.testEnabled = false;
		state.phase = FeatureCostMeasurementPhase::MeasuringCurrent;
		state.phaseStartTime = currentTime;
	}

	void ApplyFeatureCostMeasurementTestState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature)
			return;

		feature->SetPerformanceCostMeasurementEnabled(state.testEnabled);
		state.testStateApplied = true;
	}

	void RestoreFeatureCostMeasurementOriginalState(Feature* feature, FeatureCostMeasurementState& state)
	{
		if (!feature || !state.testStateApplied)
			return;

		feature->RestorePerformanceCostMeasurementState(state.originalState);
		state.testStateApplied = false;
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
			if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent)
				state.currentSample = {};
			else if (state.phase == FeatureCostMeasurementPhase::MeasuringTest)
				state.testSample = {};

			state.phaseStartTime = currentTime;
			return;
		}

		const double elapsed = currentTime - state.phaseStartTime;

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
			AddFeatureCostSample(state.currentSample, current);
			if (elapsed >= kFeatureCostMeasurementSeconds) {
				if (feature->RequiresMenuCloseForPerformanceCostMeasurement(state.testEnabled)) {
					state.phase = FeatureCostMeasurementPhase::AwaitingMenuClose;
					state.menuCloseTick = 0;
				} else {
					ApplyFeatureCostMeasurementTestState(feature, state);
					state.phase = FeatureCostMeasurementPhase::MeasuringTest;
					state.phaseStartTime = currentTime;
				}
			}
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
			AddFeatureCostSample(state.testSample, current);
			if (elapsed >= kFeatureCostMeasurementSeconds) {
				FinalizeFeatureCostMeasurement(state);
				if (feature->RequiresMenuCloseForPerformanceCostMeasurementRestore(state.originalState)) {
					state.phase = FeatureCostMeasurementPhase::AwaitingRestoreMenuClose;
					state.menuCloseTick = 0;
				} else {
					RestoreFeatureCostMeasurementOriginalState(feature, state);
					state.phase = FeatureCostMeasurementPhase::Complete;
				}
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
		if (ImGui::BeginTable("##PerformanceTuningTopCounters", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			RenderMetricCounter("Game", "Game:", summary.frameMs, "%.2f ms", highlightState.frameDirection, summary.frameMs > 0.0f);
			ImGui::TableNextColumn();
			RenderMetricCounter("GPU", "GPU:", summary.gameGpuMs, "%.2f ms", highlightState.gpuTotalDirection, summary.hasGameGpu);
			ImGui::TableNextColumn();
			RenderMetricCounter("CPU", "CPU:", summary.gameCpuMs, "%.2f ms", highlightState.cpuTotalDirection, summary.hasGameCpu);
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
		if (feature && feature->GetShortName() == "Skylighting")
			return "fastest state";

		return "Off";
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
			ImGui::TextUnformatted(
				"Measures current settings against the comparison state using game timing.");
		}
		if (!running && anyMeasurementRunning && !IsFeatureCostMeasurementActive(state)) {
			ImGui::SameLine();
			ImGui::TextDisabled("Finish the current measurement first");
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingMenuClose) {
			ImGui::Spacing();
			ImGui::TextDisabled("Close the Community Shaders menu now.");
			ImGui::TextWrapped(
				"Wait at least %.0f seconds with the menu closed. Reopen only after FPS and frame times look stable again, then continue the second measurement.",
				feature->GetPerformanceCostMeasurementMenuCloseWaitMs() / 1000.0f);
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingContinue) {
			const ULONGLONG waitMs = feature->GetPerformanceCostMeasurementMenuCloseWaitMs();
			const ULONGLONG elapsedCloseMs = state.menuCloseTick > 0 ? GetTickCount64() - state.menuCloseTick : 0;
			const bool closeWaitSatisfied = state.menuCloseTick > 0 && elapsedCloseMs >= waitMs;
			const bool readyToContinue = closeWaitSatisfied && feature->IsPerformanceCostMeasurementReady();

			ImGui::Spacing();
			if (state.menuCloseTick == 0) {
				ImGui::TextDisabled("Close the Community Shaders menu and wait %.0f seconds.", waitMs / 1000.0f);
			} else if (!closeWaitSatisfied) {
				const double remainingSeconds = static_cast<double>(waitMs - elapsedCloseMs) / 1000.0;
				ImGui::TextDisabled("Wait %.1f more seconds with the menu closed, then reopen.", remainingSeconds);
			} else if (!feature->IsPerformanceCostMeasurementReady()) {
				ImGui::TextDisabled("%s", feature->GetPerformanceCostMeasurementWaitText());
			} else {
				ImGui::TextDisabled("FPS and frame times should be stable again before continuing.");
			}

			ImGui::BeginDisabled(!readyToContinue);
			if (ImGui::Button("Continue measurement")) {
				state.testSample = {};
				state.phase = FeatureCostMeasurementPhase::MeasuringTest;
				state.phaseStartTime = ImGui::GetTime();
			}
			ImGui::EndDisabled();
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose) {
			ImGui::Spacing();
			ImGui::TextDisabled("The test is done.");
			ImGui::TextWrapped(
				"Close the Community Shaders menu now so your previous settings can be restored safely. Reopen after FPS and frame times look stable to see the result.");
			return;
		}

		if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent ||
			state.phase == FeatureCostMeasurementPhase::MeasuringTest) {
			if (!feature->IsPerformanceCostMeasurementReady()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", feature->GetPerformanceCostMeasurementWaitText());
				return;
			}

			const double elapsed = std::clamp(ImGui::GetTime() - state.phaseStartTime, 0.0, kFeatureCostMeasurementSeconds);
			ImGui::SameLine();
			if (state.phase == FeatureCostMeasurementPhase::MeasuringCurrent) {
				ImGui::TextDisabled("Measuring current %.1f / %.1fs", elapsed, kFeatureCostMeasurementSeconds);
			} else {
				ImGui::TextDisabled(
					"Measuring %s %.1f / %.1fs",
					GetFeatureCostComparisonLabel(feature, state),
					elapsed,
					kFeatureCostMeasurementSeconds);
			}
			return;
		}

		if (state.phase != FeatureCostMeasurementPhase::Complete)
			return;

		if (!state.delta.hasFrame && !state.delta.hasFps && !state.delta.hasGameGpu && !state.delta.hasGameCpu) {
			ImGui::SameLine();
			ImGui::TextDisabled("No game timing data");
			return;
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Current vs %s", GetFeatureCostComparisonLabel(feature, state));
		if (state.delta.hasFrame)
			RenderDeltaMetric(
				"Game",
				state.delta.frameMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.frameMs),
				"%+.3f ms");
		if (state.delta.hasGameGpu)
			RenderDeltaMetric(
				"GPU",
				state.delta.gameGpuMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameGpuMs),
				"%+.3f ms");
		if (state.delta.hasGameCpu)
			RenderDeltaMetric(
				"CPU",
				state.delta.gameCpuMs,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.gameCpuMs),
				"%+.3f ms");
		if (state.delta.hasFps)
			RenderDeltaMetric(
				"FPS cost",
				state.delta.fpsCost,
				GetDirectionFromFeatureCostFrameTimeDelta(state.delta.fpsCost),
				"%+.1f");
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

	std::vector<Feature*> BuildPerformanceFeatureList()
	{
		std::vector<Feature*> features;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature || !feature->loaded || feature->IsHiddenFromUserView() || !feature->IsInMenu() || !feature->HasPerformanceSettings())
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

	Feature* FindFeatureByShortName(const std::string& shortName)
	{
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->GetShortName() == shortName)
				return feature;
		}

		return nullptr;
	}

	void CancelFeatureCostMeasurement(Feature* feature, FeatureCostMeasurementState& state, bool allowPendingRestore)
	{
		if (!IsFeatureCostMeasurementActive(state)) {
			return;
		}

		if (feature && state.testStateApplied) {
			if (allowPendingRestore && feature->RequiresMenuCloseForPerformanceCostMeasurementRestore(state.originalState)) {
				state.phase = FeatureCostMeasurementPhase::AwaitingRestoreMenuClose;
				state.discardAfterRestore = true;
				state.menuCloseTick = 0;
				return;
			}

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

		if (state.baseline.hasGameGpu && current.hasGameGpu)
			state.gpuTotalDirection = GetDirectionFromFrameTimeDelta(current.gameGpuMs - state.baseline.gameGpuMs);
		if (state.baseline.hasGameCpu && current.hasGameCpu)
			state.cpuTotalDirection = GetDirectionFromFrameTimeDelta(current.gameCpuMs - state.baseline.gameCpuMs);
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

	const bool advancedPerformance = Menu::GetSingleton()->IsAdvancedUiMode();
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
			selectedFeature->DrawPerformanceSettings(advancedPerformance);
			ImGui::EndGroup();
			ImGui::EndDisabled();
			const json settingsStateAfter = selectedFeature->CapturePerformanceSettingsState();
			const bool settingsEdited = settingsStateBefore != settingsStateAfter;
			if (settingsEdited) {
				RegisterSettingsEdit(highlightState, timingBeforeSettings, frameCount);
				ClearCompletedFeatureCostMeasurement(selectedCostState);
			}
			RenderFeatureCostMeasurement(selectedFeature, selectedCostState);
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(2);
		if (ImGui::BeginChild("##PerformanceProfileChild", ImVec2(0, 0), false)) {
			ImGui::SeparatorText("Profiling");
			const auto selectedHighlight = BuildSelectedHighlight(highlightState, selectedFeature->GetShortName());
			const auto profilingPrefixes = BuildProfilingPrefixesForFeature(selectedFeature->GetShortName());
			ProfilingRenderer::RenderFeaturePerformanceSummary(profilingPrefixes, &selectedHighlight, &timingBeforeSettings);
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

		if (!(IsFeatureCostMeasurementRunning(state) || (includePending && IsFeatureCostMeasurementPending(state)))) {
			continue;
		}

		CancelFeatureCostMeasurement(FindFeatureByShortName(shortName), state, includePending);
	}

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
			state.phase = FeatureCostMeasurementPhase::AwaitingContinue;
			state.menuCloseTick = now;
		} else if (state.phase == FeatureCostMeasurementPhase::AwaitingContinue) {
			state.menuCloseTick = now;
		} else if (state.phase == FeatureCostMeasurementPhase::AwaitingRestoreMenuClose) {
			auto* feature = FindFeatureByShortName(shortName);
			if (!feature) {
				state = {};
				continue;
			}

			RestoreFeatureCostMeasurementOriginalState(feature, state);

			if (state.discardAfterRestore)
				state = {};
			else
				state.phase = FeatureCostMeasurementPhase::Complete;
		}
	}
}

bool PerformanceTuningRenderer::HasActiveMeasurements()
{
	return IsAnyFeatureCostMeasurementRunning();
}
