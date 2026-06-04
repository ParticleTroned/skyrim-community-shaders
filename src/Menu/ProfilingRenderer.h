#pragma once

#include <string>
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

	static void RenderStatistics(bool showTable = true, bool showModeToggle = true);
	static void RenderFeatureTimers(const std::string& featurePrefix);

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

	static inline std::unordered_map<std::string, ImU32> groupColorMap;
	static inline size_t nextColorIndex = 0;

	static ImU32 GetGroupColor(const std::string& groupName);
	static uint32_t ToLegitColor(ImU32 imColor);
	static ImVec4 HeatColor(float value, float maxValue);
	static void TextHeat(const char* fmt, float value, float maxValue);
	static void RenderGraph();
	static bool RenderFeatureOverview();
	static FeatureTimingData CollectFeatureTimingData(const std::string& featurePrefix, bool cpuMode);
	static bool RenderFeatureTimingGraph(const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, int graphHeight);
	static bool RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable);
};
