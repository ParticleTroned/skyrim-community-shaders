#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Profiler.h"
#include "Utils/GraphSampleGate.h"
#include "Utils/LegitProfiler.h"

class ProfilingRenderer
{
public:
	enum class TimingMode
	{
		GPU,
		CPU
	};

	enum class FeatureTimingMode
	{
		Off,
		GPU,
		CPU
	};

	struct PerformanceTimingTotals
	{
		float gpuAvgMs = 0.0f;
		float cpuAvgMs = 0.0f;
		bool hasGpu = false;
		bool hasCpu = false;
	};

	struct PerformanceTimingSummary
	{
		float presentIntervalMs = 0.0f;
		float fps = 0.0f;
		float presentIntervalSampleMs = 0.0f;
		float wholeFrameGpuMs = 0.0f;
		float wholeFrameCpuMs = 0.0f;
		float wholeFrameGpuSampleMs = 0.0f;
		float wholeFrameCpuSampleMs = 0.0f;
		uint64_t presentIntervalSampleId = 0;
		uint64_t wholeFrameSampleId = 0;
		uint64_t wholeFramePresentIntervalSampleId = 0;
		uint64_t wholeFrameGpuSampleId = 0;
		uint64_t wholeFrameCpuSampleId = 0;
		uint64_t presentDiscontinuityEpoch = 0;
		uint64_t skippedWholeFrameCaptureCount = 0;
		bool hasPresentInterval = false;
		bool hasPresentIntervalSample = false;
		bool presentIntervalSynced = false;
		bool hasWholeFrameGpu = false;
		bool hasWholeFrameCpu = false;
		bool hasWholeFrameGpuSample = false;
		bool hasWholeFrameCpuSample = false;
		bool valid = false;
		std::unordered_map<std::string, PerformanceTimingTotals> features;
	};

	struct PerformanceTimingHighlight
	{
		int frameDirection = 0;
		int fpsDirection = 0;
		int gpuTotalDirection = 0;
		int cpuTotalDirection = 0;
		int featureGpuDirection = 0;
		int featureCpuDirection = 0;
	};

	/** Shared runtime capture switch; returns whether profiling is enabled. */
	static bool RenderEnabledControl();
	static void RenderStatistics(bool showTable = true, bool showModeToggle = true, bool showEnabledToggle = true);
	/** Identifies feature-owned and shared profiling views before samples exist. */
	static bool CanProfileFeature(std::string_view a_feature);
	/** Null when the main profiling switch permits live views; otherwise explains how to unlock them. */
	static const char* GetProfilingDisabledReason();
	static bool HasFeatureTimers(const std::string& featurePrefix);
	/** Draws Off/GPU/CPU controls and live timing graphs for one feature. */
	static void RenderFeatureTimers(const std::string& featurePrefix);
	static PerformanceTimingSummary CapturePerformanceTimingSummary(const std::vector<std::string>& featurePrefixes,
		Profiler::CaptureMode captureMode = Profiler::CaptureMode::DetailedPasses);
	/** Draws both feature timing views, preserving owned versus shared attribution. */
	static void RenderFeaturePerformanceSummary(const std::string& featurePrefix, const PerformanceTimingHighlight* highlight = nullptr);
	static void RenderFeaturePerformanceSummary(const std::vector<std::string>& featurePrefixes, const PerformanceTimingHighlight* highlight = nullptr);

private:
	static constexpr uint32_t kFeatureGraphHistorySize = 60;
	static inline TimingMode timingMode = TimingMode::GPU;
	static inline float timeSinceLastUpdate = 0.0f;
	static inline float lastFrameTime = 0.0f;
	static inline uint64_t statisticsHistoryEpoch = 0;

	struct PassEntry
	{
		std::string label;
		float avgMs;
		float p95Ms;
		float p99Ms;
	};
	struct GroupEntry
	{
		std::string name;
		float totalAvgMs = 0.0f;
		float totalP95Ms = 0.0f;
		float totalP99Ms = 0.0f;
		std::vector<PassEntry> passes;
	};
	static inline float cachedTotalAvgMs = 0.0f;
	static inline float cachedTotalP95Ms = 0.0f;
	static inline float cachedTotalP99Ms = 0.0f;
	static inline float cachedMaxAvgMs = 0.0f;
	static inline float cachedMaxP95Ms = 0.0f;
	static inline float cachedMaxP99Ms = 0.0f;
	static inline std::vector<GroupEntry> cachedGroups;

	static inline ImGuiUtils::ProfilerGraph gpuGraph{ Profiler::kHistorySize };
	static inline Util::GraphSampleGate gpuGraphGate;

	struct FeatureGraphState
	{
		ImGuiUtils::ProfilerGraph gpuGraph{ kFeatureGraphHistorySize };
		ImGuiUtils::ProfilerGraph cpuGraph{ kFeatureGraphHistorySize };
		Util::GraphSampleGate gpuGate;
		Util::GraphSampleGate cpuGate;
	};
	struct FeatureTimingEntry
	{
		std::string label;
		std::string colorKey;
		float timeMs;
		float avgMs;
		float p95Ms;
		float p99Ms;
	};
	enum class TimingAttribution
	{
		Feature,
		Shared
	};
	struct FeatureTimingData
	{
		TimingAttribution attribution = TimingAttribution::Feature;
		std::vector<FeatureTimingEntry> entries;
		float totalAvg = 0.0f;
		float totalP95 = 0.0f;
		float totalP99 = 0.0f;
		float maxAvg = 0.0f;
		float maxP95 = 0.0f;
		float maxP99 = 0.0f;
	};
	struct FeatureStatistics
	{
		FeatureTimingData data;
		double updatedAt = -1.0;
		uint64_t historyEpoch = 0;
		uint64_t externalEpoch = 0;
	};
	static inline std::unordered_map<std::string, std::array<FeatureStatistics, 2>> featureStatistics;
	static inline std::unordered_map<std::string, FeatureGraphState> featureGraphs;
	static inline std::unordered_map<std::string, FeatureTimingMode> featureTimingModes;

	static ImU32 GetGroupColor(std::string_view groupName);
	static uint32_t ToLegitColor(ImU32 imColor);
	static ImVec4 HeatColor(float value, float maxValue);
	static void TextHeat(const char* fmt, float value, float maxValue);
	static void RenderTimingModeToggle();
	static void SetupTimingTableColumns(float passColumnWidth, bool includePercentColumn);
	static void RenderGraph();
	static bool RenderFeatureOverview();
	static FeatureTimingData CollectFeatureTimingData(
		const std::string& featurePrefix,
		bool cpuMode,
		bool includePercentiles = true);
	static FeatureTimingData CollectFeatureTimingData(
		const std::vector<std::string>& featurePrefixes,
		bool cpuMode,
		bool includePercentiles = true,
		TimingAttribution attribution = TimingAttribution::Feature,
		bool useSelfTimesForTotal = false,
		const std::vector<Profiler::TimerResult>* timingResults = nullptr);
	static bool RenderFeatureTimingGraph(const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, Util::GraphSampleGate& gate, int graphHeight);
	static bool RenderTimingSection(const std::string& key, const FeatureTimingData& data, bool cpuMode, bool showTable, const FeatureTimingData* statistics = nullptr);
	static bool RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable);
};
