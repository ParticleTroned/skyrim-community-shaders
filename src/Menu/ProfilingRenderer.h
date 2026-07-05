#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Profiler.h"
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
		float gpuTotalMs = 0.0f;
		float cpuTotalMs = 0.0f;
		float gameGpuMs = 0.0f;
		float gameCpuMs = 0.0f;
		float frameMs = 0.0f;
		float fps = 0.0f;
		bool hasGameGpu = false;
		bool hasGameCpu = false;
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

	static void RenderStatistics(bool showTable = true, bool showModeToggle = true);
	static bool HasFeatureTimers(const std::string& featurePrefix);
	static void RenderFeatureTimers(const std::string& featurePrefix);
	static PerformanceTimingSummary CapturePerformanceTimingSummary(const std::vector<std::string>& featurePrefixes, bool requestCapture = true);
	static void RenderFeaturePerformanceSummary(
		const std::string& featurePrefix,
		const PerformanceTimingHighlight* highlight = nullptr,
		const PerformanceTimingSummary* summaryOverride = nullptr);
	static void RenderFeaturePerformanceSummary(
		const std::vector<std::string>& featurePrefixes,
		const PerformanceTimingHighlight* highlight = nullptr,
		const PerformanceTimingSummary* summaryOverride = nullptr);

private:
	static inline TimingMode timingMode = TimingMode::GPU;
	static inline float timeSinceLastUpdate = 0.0f;
	static inline float lastFrameTime = 0.0f;

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

	struct FeatureGraphState
	{
		ImGuiUtils::ProfilerGraph gpuGraph{ Profiler::kHistorySize };
		ImGuiUtils::ProfilerGraph cpuGraph{ Profiler::kHistorySize };
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
	struct FeatureTimingData
	{
		std::vector<FeatureTimingEntry> entries;
		float totalAvg = 0.0f;
		float totalP95 = 0.0f;
		float totalP99 = 0.0f;
		float maxAvg = 0.0f;
		float maxP95 = 0.0f;
		float maxP99 = 0.0f;
	};
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
	static FeatureTimingData CollectFeatureTimingData(const std::string& featurePrefix, bool cpuMode);
	static FeatureTimingData CollectFeatureTimingData(const std::vector<std::string>& featurePrefixes, bool cpuMode);
	static bool RenderFeatureTimingGraph(const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, int graphHeight);
	static bool RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable);
};
