#include "ProfilingRenderer.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/NvPerf.h"
#endif
#include "PerformanceTuningRenderer.h"
#include "SettingsPage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Features/PerformanceOverlay.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "RE/M/Misc.h"
#include "State.h"
#include "Util.h"
#include "Utils/FeatureProfiling.h"
#include "Utils/ProfilerTiming.h"
#include "Utils/UI.h"

static constexpr float kGraphHeadroomScale = 1.2f;
static constexpr float kMainGraphHeight = 180.0f;
static constexpr float kMainGraphMinFrameTimeSec = 0.0001f;
static constexpr float kFeatureGraphMinFrameTimeSec = 0.00001f;
static constexpr float kTimingTableMetricColumnWidth = 55.0f;
static constexpr float kTimingTablePercentColumnWidth = 45.0f;
static constexpr float kStatsRefreshSeconds = 1.0f;
static constexpr uint32_t kDisplayedRollingFrameCount = 60;
static constexpr float kMaxDisplayTimingSampleMs = 1000.0f;
static bool IsPositiveFinite(float value)
{
	return std::isfinite(value) && value > 0.0f;
}

static bool IsDisplayTimingSampleValid(float value)
{
	return std::isfinite(value) && value >= 0.0f && value <= kMaxDisplayTimingSampleMs;
}

static void TextWithTuningDelta(int direction, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (direction == 0) {
		ImGui::TextV(fmt, args);
	} else {
		ImGui::TextColoredV(Util::Color::PerformanceDelta(direction), fmt, args);
	}
	va_end(args);
}

static void RenderFeatureTimingStats(float avgMs, float p95Ms, float p99Ms, int direction)
{
	TextWithTuningDelta(
		direction,
		"%s %.3f ms",
		T("menu.performance_tuning.profiling.metric.average", "Average"),
		avgMs);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(
				"menu.performance_tuning.profiling.metric.average_tooltip",
				"Arithmetic mean over the latest detailed-capture frames."));
	}

	TextWithTuningDelta(
		direction,
		"%s %.3f ms",
		T("menu.performance_tuning.profiling.metric.p95", "P95"),
		p95Ms);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(
				"menu.performance_tuning.profiling.metric.p95_tooltip",
				"95% of the latest detailed-capture frame samples are at or below this value."));
	}

	TextWithTuningDelta(
		direction,
		"%s %.3f ms",
		T("menu.performance_tuning.profiling.metric.p99", "P99"),
		p99Ms);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(
				"menu.performance_tuning.profiling.metric.p99_tooltip",
				"99% of the latest detailed-capture frame samples are at or below this value."));
	}
}

static std::string BuildTimingPrefixKey(const std::vector<std::string>& prefixes)
{
	std::string key;
	for (const auto& prefix : prefixes) {
		if (!key.empty())
			key += '|';
		key += prefix;
	}
	return key;
}

static int ScaleToUiInt(float value)
{
	return std::max(1, static_cast<int>(std::round(value * Util::GetUIScale())));
}

static ImU32 HslToImU32(float h, float s, float l)
{
	auto hue2rgb = [](float p, float q, float t) -> float {
		if (t < 0.0f)
			t += 1.0f;
		if (t > 1.0f)
			t -= 1.0f;
		if (t < 1.0f / 6.0f)
			return p + (q - p) * 6.0f * t;
		if (t < 0.5f)
			return q;
		if (t < 2.0f / 3.0f)
			return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
		return p;
	};

	float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
	float p = 2.0f * l - q;
	float r = hue2rgb(p, q, h + 1.0f / 3.0f);
	float g = hue2rgb(p, q, h);
	float b = hue2rgb(p, q, h - 1.0f / 3.0f);

	return IM_COL32(
		static_cast<uint8_t>(r * 255.0f),
		static_cast<uint8_t>(g * 255.0f),
		static_cast<uint8_t>(b * 255.0f),
		255);
}

static uint32_t FinalizeHash(uint32_t hash)
{
	hash ^= hash >> 16;
	hash *= 0x7feb352du;
	hash ^= hash >> 15;
	hash *= 0x846ca68bu;
	hash ^= hash >> 16;
	return hash;
}

static uint32_t StableHash(std::string_view value)
{
	uint32_t hash = 2166136261u;
	for (const unsigned char c : value) {
		hash ^= c;
		hash *= 16777619u;
	}

	return FinalizeHash(hash);
}

static uint32_t MixHash(uint32_t hash, uint32_t salt)
{
	hash ^= salt + 0x9e3779b9u + (hash << 6) + (hash >> 2);
	return FinalizeHash(hash);
}

static float HashToUnitFloat(uint32_t hash)
{
	return static_cast<float>(static_cast<double>(hash) / 4294967296.0);
}

static float GetColorMarkerExtraWidth()
{
	return std::ceil(std::max(6.0f, ImGui::GetTextLineHeight() * 0.65f) + ImGui::GetStyle().ItemInnerSpacing.x);
}

static void RenderColorMarker(ImU32 color)
{
	const float lineHeight = ImGui::GetTextLineHeight();
	const float markerSize = std::max(6.0f, std::floor(lineHeight * 0.65f));
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float markerY = cursor.y + (lineHeight - markerSize) * 0.5f;
	const ImVec2 markerMin(cursor.x, markerY);
	const ImVec2 markerMax(cursor.x + markerSize, markerY + markerSize);

	auto* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(markerMin, markerMax, color, 2.0f);
	drawList->AddRect(markerMin, markerMax, ImGui::GetColorU32(ImGuiCol_Border), 2.0f);

	ImGui::Dummy(ImVec2(markerSize, lineHeight));
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

static void ReplaceAll(std::string& value, std::string_view from, std::string_view to)
{
	if (from.empty())
		return;

	size_t pos = 0;
	while ((pos = value.find(from.data(), pos, from.size())) != std::string::npos) {
		value.replace(pos, from.size(), to.data(), to.size());
		pos += to.size();
	}
}

static std::string BuildProfilerGraphLabel(std::string_view label)
{
	std::string result(label.data(), label.size());
	ReplaceAll(result, "ScreenSpaceShadows", "SSShadows");
	ReplaceAll(result, "ScreenSpace", "SS");
	ReplaceAll(result, "CommunityShaders", "CS");
	ReplaceAll(result, "SubsurfaceScattering", "SSS");
	ReplaceAll(result, "DynamicResolution", "DynRes");
	ReplaceAll(result, "Visualization", "Viz");
	ReplaceAll(result, "Composite", "Comp");
	ReplaceAll(result, "Dispatch", "Disp");
	ReplaceAll(result, "Foveated", "Fov");
	ReplaceAll(result, "Periphery", "Periph");
	ReplaceAll(result, "Temporal", "Temp");
	ReplaceAll(result, "Dynamic", "Dyn");
	ReplaceAll(result, "Resolution", "Res");
	ReplaceAll(result, "Upscaling", "Upscale");
	ReplaceAll(result, "Render", "Rnd");
	ReplaceAll(result, "Shader", "Shd");
	ReplaceAll(result, "::", ":");

	constexpr size_t kMaxGraphLabelLength = 34;
	if (result.size() > kMaxGraphLabelLength)
		result = result.substr(0, kMaxGraphLabelLength - 2) + "..";

	return result;
}

static bool TryMatchTimingPrefix(std::string_view timerName, std::string_view prefix, bool compactLabel, std::string& label)
{
	if (!Util::FeatureProfiling::Matches(timerName, prefix))
		return false;

	if (compactLabel && timerName != prefix) {
		const auto compact = timerName.substr(prefix.size() + 2);
		label.assign(compact.data(), compact.size());
	} else {
		label.assign(timerName.data(), timerName.size());
	}
	return true;
}

static int ComputeGraphLegendWidth(int totalWidth, int minGraphWidth, float widthFraction, int minLegendWidth, int maxLegendWidth)
{
	const int reservedGraphWidth = std::min(minGraphWidth, totalWidth);
	const int availableLegendWidth = std::max(0, totalWidth - reservedGraphWidth);
	if (availableLegendWidth <= 0)
		return 0;

	const int desiredLegendWidth = std::clamp(static_cast<int>(totalWidth * widthFraction), minLegendWidth, maxLegendWidth);
	return std::min(desiredLegendWidth, availableLegendWidth);
}

template <class FeatureTimingDataT>
static int ComputeFeatureGraphLegendWidth(const FeatureTimingDataT& data, int totalWidth)
{
	if (data.entries.empty())
		return 0;

	const float uiScale = Util::GetUIScale();
	constexpr float legendTextScale = 0.74f;
	const float markerAndConnectorWidth = (3.0f + 5.0f + 18.0f + 8.0f + 5.0f) * uiScale;
	const float textColumnWidth = std::max(
		48.0f * uiScale,
		ImGui::CalcTextSize("000.00ms").x * legendTextScale + 5.0f * uiScale);
	float labelWidth = 0.0f;
	for (const auto& entry : data.entries) {
		const auto label = BuildProfilerGraphLabel(entry.label);
		labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label.c_str()).x * legendTextScale);
	}

	const int desiredLegendWidth = static_cast<int>(std::ceil(markerAndConnectorWidth + textColumnWidth + labelWidth + 10.0f * uiScale));
	const int minGraphWidth = ScaleToUiInt(24.0f);
	const int availableLegendWidth = std::max(0, totalWidth - std::min(minGraphWidth, totalWidth));
	return std::min(desiredLegendWidth, availableLegendWidth);
}

static bool HasLiveTimingMode(const Profiler::TimerResult& result, bool cpuMode)
{
	return cpuMode ? result.activeCpu : result.activeGpu;
}

static bool HasAnyLiveTimingMode(const Profiler::TimerResult& result)
{
	return result.activeGpu || result.activeCpu;
}

struct DisplayTimingStats
{
	float timeMs = 0.0f;
	float avgMs = 0.0f;
	float p95Ms = 0.0f;
	float p99Ms = 0.0f;
};

enum class DisplayTimingContribution
{
	Self,
	Outermost
};

static uint32_t CollectDisplayTimingSamples(
	const Profiler::TimerResult& result,
	bool cpuMode,
	std::array<float, kDisplayedRollingFrameCount>& samples,
	DisplayTimingContribution contribution = DisplayTimingContribution::Self)
{
	samples.fill(0.0f);

	const uint32_t historyCount = cpuMode ? result.cpuHistoryCount : result.historyCount;
	if (historyCount == 0)
		return 0;

	std::array<float, kDisplayedRollingFrameCount> collectedSamples{};
	uint32_t sampleCount = 0;
	const bool outermost = contribution == DisplayTimingContribution::Outermost;
	for (uint32_t offset = 0; offset < historyCount && sampleCount < kDisplayedRollingFrameCount; ++offset) {
		const uint32_t historyIndex = historyCount - 1 - offset;
		const float sample = cpuMode ?
		                         (outermost ? result.GetOutermostCpuHistorySample(historyIndex) : result.GetCpuHistorySample(historyIndex)) :
		                         (outermost ? result.GetOutermostGpuHistorySample(historyIndex) : result.GetHistorySample(historyIndex));
		if (!IsDisplayTimingSampleValid(sample))
			continue;

		collectedSamples[kDisplayedRollingFrameCount - 1 - sampleCount] = sample;
		sampleCount++;
	}

	const uint32_t sourceOffset = kDisplayedRollingFrameCount - sampleCount;
	for (uint32_t i = 0; i < sampleCount; ++i)
		samples[i] = collectedSamples[sourceOffset + i];

	return sampleCount;
}

static DisplayTimingStats ComputeDisplayTimingStats(
	const std::array<float, kDisplayedRollingFrameCount>& samples,
	uint32_t sampleCount,
	bool includePercentiles = true)
{
	const auto stats = Util::ProfilerTiming::CalculateStatistics(
		{ samples.data(), sampleCount }, includePercentiles);
	return { stats.average, stats.average, stats.p95, stats.p99 };
}

static bool TryGetDisplayTimingStats(const Profiler::TimerResult& result, bool cpuMode, DisplayTimingStats& stats)
{
	std::array<float, kDisplayedRollingFrameCount> samples{};
	const uint32_t sampleCount = CollectDisplayTimingSamples(result, cpuMode, samples);
	if (sampleCount == 0)
		return false;

	stats = ComputeDisplayTimingStats(samples, sampleCount);
	return true;
}

struct DisplayTimingSampleAccumulator
{
	void Add(const std::array<float, kDisplayedRollingFrameCount>& sourceSamples, uint32_t sourceSampleCount)
	{
		if (sourceSampleCount == 0)
			return;

		sampleCount = std::max(sampleCount, sourceSampleCount);
		const uint32_t sampleOffset = kDisplayedRollingFrameCount - sourceSampleCount;
		for (uint32_t i = 0; i < sourceSampleCount; ++i)
			samples[sampleOffset + i] += sourceSamples[i];
	}

	[[nodiscard]] DisplayTimingStats GetStats(bool includePercentiles = true) const
	{
		if (sampleCount == 0)
			return {};

		std::array<float, kDisplayedRollingFrameCount> compactSamples{};
		const uint32_t sampleOffset = kDisplayedRollingFrameCount - sampleCount;
		for (uint32_t i = 0; i < sampleCount; ++i)
			compactSamples[i] = samples[sampleOffset + i];

		return ComputeDisplayTimingStats(compactSamples, sampleCount, includePercentiles);
	}

	[[nodiscard]] bool HasSamples() const { return sampleCount > 0; }

	std::array<float, kDisplayedRollingFrameCount> samples{};
	uint32_t sampleCount = 0;
};

static float GetTimingTableMetricWidth()
{
	return std::max(kTimingTableMetricColumnWidth * Util::GetUIScale(), ImGui::CalcTextSize("000.000").x);
}

static float GetTextColumnWidth(const char* header, const std::vector<std::string>& labels, float extraWidth = 0.0f)
{
	float width = ImGui::CalcTextSize(header).x;
	for (const auto& label : labels)
		width = std::max(width, ImGui::CalcTextSize(label.c_str()).x);

	return std::ceil(width + ImGui::GetStyle().CellPadding.x * 2.0f + extraWidth);
}

ImU32 ProfilingRenderer::GetGroupColor(std::string_view groupName)
{
	const uint32_t hash = StableHash(groupName);
	const float hue = HashToUnitFloat(hash);
	const float saturation = 0.68f + HashToUnitFloat(MixHash(hash, 0xA511E9B3u)) * 0.12f;
	const float lightness = 0.50f + HashToUnitFloat(MixHash(hash, 0x63D83595u)) * 0.10f;
	return HslToImU32(hue, saturation, lightness);
}

uint32_t ProfilingRenderer::ToLegitColor(ImU32 imColor)
{
	uint8_t r = (imColor >> 0) & 0xFF;
	uint8_t g = (imColor >> 8) & 0xFF;
	uint8_t b = (imColor >> 16) & 0xFF;
	return (0xFF << 24) | (b << 16) | (g << 8) | r;
}

ImVec4 ProfilingRenderer::HeatColor(float value, float maxValue)
{
	if (maxValue <= 0.0f)
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

	float x = std::clamp(value / maxValue, 0.0f, 1.0f);

	float x2 = x * x;
	float x3 = x2 * x;
	float x4 = x2 * x2;
	float x5 = x3 * x2;

	float r = 0.13572138f + 4.61539260f * x - 42.66032258f * x2 + 132.13108234f * x3 - 152.94239396f * x4 + 59.28637943f * x5;
	float g = 0.09140261f + 2.19418839f * x + 4.84296658f * x2 - 14.18503333f * x3 + 4.27729857f * x4 + 2.82956604f * x5;
	float b = 0.10667330f + 12.64194608f * x - 60.58204836f * x2 + 110.36276771f * x3 - 89.90310912f * x4 + 27.34824973f * x5;

	float alpha = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w;

	return ImVec4(std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), alpha);
}

void ProfilingRenderer::TextHeat(const char* fmt, float value, float maxValue)
{
	ImVec4 bg = HeatColor(value, maxValue);
	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), fmt, value);
}

void ProfilingRenderer::RenderTimingModeToggle()
{
	int mode = static_cast<int>(timingMode);

	const MenuUI::Choice choices[] = {
		{ "gpu", "GPU", "Graphics-card timings", "Inspect render-pass cost." },
		{ "cpu", "CPU", "Processor timings", "Inspect processor work." }
	};
	const int chosen = MenuUI::ChoiceCards("ProfilingTimingMode", timingMode == TimingMode::GPU ? 0 : 1, choices);
	if (chosen >= 0)
		mode = static_cast<int>(chosen == 0 ? TimingMode::GPU : TimingMode::CPU);

	const auto newMode = static_cast<TimingMode>(mode);
	if (newMode != timingMode) {
		timingMode = newMode;
		timeSinceLastUpdate = kStatsRefreshSeconds;
		gpuGraph = ImGuiUtils::ProfilerGraph{ Profiler::kHistorySize };
		gpuGraphGate.Reset();
	}
}

void ProfilingRenderer::SetupTimingTableColumns(float passColumnWidth, bool includePercentColumn)
{
	const float scale = Util::GetUIScale();
	const float metricWidth = GetTimingTableMetricWidth();
	ImGui::TableSetupColumn(T("menu.profiling.pass", "Pass"), passColumnWidth > 0 ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, passColumnWidth);
	ImGui::TableSetupColumn(T("menu.profiling.avg", "Avg"), ImGuiTableColumnFlags_WidthFixed, metricWidth);
	ImGui::TableSetupColumn(T("menu.profiling.p95", "P95"), ImGuiTableColumnFlags_WidthFixed, metricWidth);
	ImGui::TableSetupColumn(T("menu.profiling.p99", "P99"), ImGuiTableColumnFlags_WidthFixed, metricWidth);
	if (includePercentColumn)
		ImGui::TableSetupColumn("%%", ImGuiTableColumnFlags_WidthFixed, kTimingTablePercentColumnWidth * scale);
}

void ProfilingRenderer::RenderGraph()
{
	auto& profiler = (*globals::profiler);
	const auto& results = profiler.GetResults(false);
	bool cpuMode = (timingMode == TimingMode::CPU);

	if (results.empty())
		return;

	std::vector<legit::ProfilerTask> tasks;

	double accumulated = 0.0;
	for (const auto& result : results) {
		if (!result.valid || !HasLiveTimingMode(result, cpuMode))
			continue;

		std::array<float, kDisplayedRollingFrameCount> samples{};
		const uint32_t sampleCount = CollectDisplayTimingSamples(result, cpuMode, samples);
		if (sampleCount == 0)
			continue;

		const auto stats = ComputeDisplayTimingStats(samples, sampleCount, false);
		float timeMs = stats.timeMs;

		std::string groupName;
		auto pos = result.name.find("::");
		if (pos != std::string::npos)
			groupName = result.name.substr(0, pos);
		else
			groupName = result.name;

		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + timeMs) / 1000.0;
		task.name = result.name;
		task.displayName = BuildProfilerGraphLabel(result.name);
		task.color = ToLegitColor(GetGroupColor(groupName));
		tasks.push_back(task);
		accumulated += timeMs;
	}

	if (tasks.empty())
		return;

	if (globals::state && gpuGraphGate.ShouldSample(globals::state->frameCount, profiler.GetHistoryEpoch()))
		gpuGraph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = gpuGraph.GetPeakFrameTime() * kGraphHeadroomScale;
	if (maxFrameTimeSec < kMainGraphMinFrameTimeSec)
		maxFrameTimeSec = kMainGraphMinFrameTimeSec;

	const float uiScale = Util::GetUIScale();
	const int totalWidth = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
	const int legendWidth = ComputeGraphLegendWidth(totalWidth, ScaleToUiInt(100.0f), 0.42f, ScaleToUiInt(280.0f), ScaleToUiInt(420.0f));
	const int graphWidth = std::max(1, totalWidth - legendWidth);
	const float graphHeight = kMainGraphHeight * uiScale;

	gpuGraph.RenderTimings(static_cast<float>(graphWidth), static_cast<float>(legendWidth), graphHeight, 0, maxFrameTimeSec, uiScale);

	ImGui::Spacing();
}

ProfilingRenderer::FeatureTimingData ProfilingRenderer::CollectFeatureTimingData(
	const std::string& featurePrefix,
	bool cpuMode,
	bool includePercentiles)
{
	const auto* view = Util::FeatureProfiling::Find(featurePrefix);
	if (view && !view->HasOwnedTimings(cpuMode))
		return {};
	std::vector<std::string> prefixes;
	if (view) {
		if (!view->ownedRoot.empty())
			prefixes.emplace_back(view->ownedRoot);
		for (auto prefix : view->ownedPrefixes)
			prefixes.emplace_back(prefix);
	} else {
		prefixes.push_back(featurePrefix);
	}
	return CollectFeatureTimingData(prefixes, cpuMode, includePercentiles, TimingAttribution::Feature, view && !view->ownedPrefixes.empty());
}

ProfilingRenderer::FeatureTimingData ProfilingRenderer::CollectFeatureTimingData(
	const std::vector<std::string>& featurePrefixes,
	bool cpuMode,
	bool includePercentiles,
	TimingAttribution attribution,
	bool useSelfTimesForTotal,
	const std::vector<Profiler::TimerResult>* timingResults)
{
	FeatureTimingData data;
	data.attribution = attribution;
	if (!globals::profiler || !globals::profiler->IsInitialized())
		return data;
	const auto& results = timingResults ? *timingResults : globals::profiler->GetResults(false);
	DisplayTimingSampleAccumulator totalSamples;
	const bool compactLabel = featurePrefixes.size() == 1;
	for (const auto& r : results) {
		if (!r.valid || !HasLiveTimingMode(r, cpuMode))
			continue;

		std::string label;
		for (const auto& prefix : featurePrefixes) {
			if (TryMatchTimingPrefix(r.name, prefix, compactLabel, label))
				break;
		}
		if (label.empty())
			continue;

		std::array<float, kDisplayedRollingFrameCount> samples{};
		const uint32_t sampleCount = CollectDisplayTimingSamples(r, cpuMode, samples);
		if (sampleCount == 0)
			continue;

		const auto stats = ComputeDisplayTimingStats(samples, sampleCount, includePercentiles);
		float timeMs = stats.timeMs;
		float avg = stats.avgMs;
		float p95 = stats.p95Ms;
		float p99 = stats.p99Ms;
		data.entries.push_back({ label, r.name, timeMs, avg, p95, p99 });
		data.maxAvg = std::max(data.maxAvg, avg);
		data.maxP95 = std::max(data.maxP95, p95);
		data.maxP99 = std::max(data.maxP99, p99);
		if (attribution == TimingAttribution::Feature) {
			// Selected passes can nest across roots or inside an unselected parent.
			const auto contribution = useSelfTimesForTotal ? DisplayTimingContribution::Self : DisplayTimingContribution::Outermost;
			std::array<float, kDisplayedRollingFrameCount> contributionSamples{};
			const uint32_t contributionSampleCount = CollectDisplayTimingSamples(r, cpuMode, contributionSamples, contribution);
			totalSamples.Add(contributionSamples, contributionSampleCount);
		}
	}

	const auto totalStats = totalSamples.GetStats(includePercentiles);
	data.totalAvg = totalStats.avgMs;
	data.totalP95 = totalStats.p95Ms;
	data.totalP99 = totalStats.p99Ms;

	data.maxAvg = std::max(data.maxAvg, data.totalAvg);
	data.maxP95 = std::max(data.maxP95, data.totalP95);
	data.maxP99 = std::max(data.maxP99, data.totalP99);

	return data;
}

bool ProfilingRenderer::RenderFeatureTimingGraph(const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, Util::GraphSampleGate& gate, int graphHeight)
{
	(void)featurePrefix;

	if (data.entries.empty())
		return false;

	std::vector<legit::ProfilerTask> tasks;
	double accumulated = 0.0;
	for (const auto& e : data.entries) {
		legit::ProfilerTask task;
		task.startTime = accumulated / 1000.0;
		task.endTime = (accumulated + e.timeMs) / 1000.0;
		task.name = e.colorKey;
		task.displayName = BuildProfilerGraphLabel(e.label);
		task.color = ToLegitColor(GetGroupColor(e.colorKey));
		tasks.push_back(task);
		accumulated += e.timeMs;
	}

	if (tasks.empty())
		return false;

	if (globals::state && gate.ShouldSample(globals::state->frameCount, globals::profiler->GetHistoryEpoch()))
		graph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = graph.GetPeakFrameTime() * kGraphHeadroomScale;
	if (maxFrameTimeSec < kFeatureGraphMinFrameTimeSec)
		maxFrameTimeSec = kFeatureGraphMinFrameTimeSec;

	const float uiScale = Util::GetUIScale();
	const int totalWidth = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
	const int legendWidth = ComputeFeatureGraphLegendWidth(data, totalWidth);
	const int graphWidth = std::max(1, totalWidth - legendWidth);
	const float scaledGraphHeight = static_cast<float>(graphHeight) * uiScale;

	graph.RenderTimings(static_cast<float>(graphWidth), static_cast<float>(legendWidth), scaledGraphHeight, 0, maxFrameTimeSec, uiScale);
	return true;
}

bool ProfilingRenderer::RenderTimingSection(const std::string& key, const FeatureTimingData& data, bool cpuMode, bool showTable, const FeatureTimingData* statistics)
{
	const bool shared = data.attribution == TimingAttribution::Shared;
	if (data.entries.empty()) {
		ImGui::TextDisabled(T("menu.profiling.no_samples_for_these_passes_in_the_current_scene", "No samples for these passes in the current scene."));
		return false;
	}

	ImGui::PushID(key.c_str());
	const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
	if (!shared) {
		auto& state = featureGraphs[key];
		auto& graph = cpuMode ? state.cpuGraph : state.gpuGraph;
		auto& gate = cpuMode ? state.cpuGate : state.gpuGate;
		if (RenderFeatureTimingGraph(key, data, graph, gate, 100))
			ImGui::Spacing();
	}

	if (!showTable && !shared)
		ImGui::Text(T("menu.profiling.instrumented_average_3f_ms", "Instrumented average: %.3f ms"), data.totalAvg);

	if (showTable || shared) {
		const auto& table = statistics ? *statistics : data;
		std::vector<std::string> passLabels;
		passLabels.reserve(table.entries.size() + 1);
		for (const auto& e : table.entries)
			passLabels.push_back(e.label);
		if (!shared)
			passLabels.emplace_back("Instrumented subtotal");
		const float metricAndPaddingWidth = GetTimingTableMetricWidth() * 3 + ImGui::GetStyle().CellPadding.x * 8;
		const bool scroll = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 8 + metricAndPaddingWidth;
		const float innerWidth = scroll ? GetTextColumnWidth("Pass", passLabels, GetColorMarkerExtraWidth()) + metricAndPaddingWidth : 0;
		const float height = scroll ? static_cast<float>(passLabels.size() + 1) * (ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2) + ImGui::GetStyle().ScrollbarSize + 2 : 0;
		if (!ImGui::BeginTable("##FeatureTimers", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | (scroll ? ImGuiTableFlags_ScrollX : ImGuiTableFlags_None), { 0, height }, innerWidth))
			return true;
		const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
		SetupTimingTableColumns(0, false);
		ImGui::TableHeadersRow();

		for (const auto& e : table.entries) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			RenderColorMarker(GetGroupColor(e.colorKey));
			ImGui::TextWrapped("%s", e.label.c_str());
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.avgMs, table.maxAvg);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p95Ms, table.maxP95);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p99Ms, table.maxP99);
		}

		if (!shared) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "Instrumented subtotal");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped(T("menu.profiling.subtotal_excludes_nesting_overlap_each_row_is_self_time_excluding", "Subtotal excludes nesting overlap. Each row is self time, excluding profiled descendants."));
			ImGui::TableNextColumn();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", table.totalAvg);
			ImGui::TableNextColumn();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", table.totalP95);
			ImGui::TableNextColumn();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", table.totalP99);
		}
	}
	return true;
}

bool ProfilingRenderer::RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable)
{
	const bool cpuMode = featureMode == FeatureTimingMode::CPU;
	ImGui::PushID(cpuMode ? "CPU" : "GPU");
	const SKSE::stl::scope_exit restoreMode([] { ImGui::PopID(); });
	const auto* view = Util::FeatureProfiling::Find(featurePrefix);
	const auto renderSection = [&](const std::string& key, bool table, const auto& collect, uint64_t externalEpoch = 0) {
		const auto live = collect(false);
		if (!table)
			return RenderTimingSection(key, live, cpuMode, false);
		auto& cache = featureStatistics[key][cpuMode ? 1 : 0];
		const double now = ImGui::GetTime();
		const uint64_t epoch = globals::profiler->GetHistoryEpoch();
		const bool samePasses = std::equal(live.entries.begin(), live.entries.end(),
			cache.data.entries.begin(), cache.data.entries.end(),
			[](const auto& left, const auto& right) { return left.colorKey == right.colorKey; });
		if (cache.updatedAt < 0.0 || now < cache.updatedAt || now - cache.updatedAt >= kStatsRefreshSeconds ||
			cache.historyEpoch != epoch || cache.externalEpoch != externalEpoch || !samePasses) {
			cache.data = collect(true);
			cache.updatedAt = now;
			cache.historyEpoch = epoch;
			cache.externalEpoch = externalEpoch;
		}
		return RenderTimingSection(key, live, cpuMode, true, &cache.data);
	};
	bool rendered = false;
	if (!view || view->HasOwnedTimings(cpuMode)) {
		MenuUI::SectionHeading(cpuMode ? T("menu.profiling.feature_cpu_timings_ms", "Feature CPU timings (ms)") : T("menu.profiling.feature_gpu_timings_ms", "Feature GPU timings (ms)"));
		rendered = renderSection(featurePrefix, showTable,
			[&](bool statistics) { return CollectFeatureTimingData(featurePrefix, cpuMode, statistics); });
	}
	if (!cpuMode && featurePrefix == "NeuralRendering") {
		MenuUI::SectionHeading(T("menu.profiling.nr_inference_gpu_timings_d3d12_ms", "NR inference GPU timings (D3D12, ms)"));
		MenuUI::DetailText(T("menu.profiling.latest_completed_native_inference_work", "Latest completed native inference work, including feature creation when required. Summed per frame; overlaps feature GPU timings and is excluded from their subtotal."));
		const auto timings = globals::profiler->GetExternalGpuTimings();
		const auto sampled = std::find_if(timings.begin(), timings.end(), [&](const auto& timing) { return timing.name.starts_with(featurePrefix + "::"); });
		if (sampled != timings.end())
			ImGui::TextDisabled(T("menu.profiling.latest_sampled_frame_u", "Latest sampled frame: %u"), sampled->frameId);
		std::vector<Profiler::TimerResult> results;
		for (const auto& timing : timings) {
			Profiler::TimerResult result;
			result.name = timing.name;
			result.valid = result.hasGpu = result.activeGpu = timing.history.count != 0;
			result.historyBuffer = timing.history.history;
			result.historyHead = timing.history.head;
			result.historyCount = timing.history.count;
			results.push_back(std::move(result));
		}
		rendered = renderSection(featurePrefix + "::inference", true, [&](bool statistics) { return CollectFeatureTimingData({ featurePrefix }, false, statistics, TimingAttribution::Shared, true, &results); }, globals::profiler->GetGpuCaptureEpoch()) || rendered;
	}
	if (view && !view->sharedPrefixes.empty()) {
		MenuUI::SectionHeading(cpuMode ? T("menu.profiling.shared_cpu_pass_timings_ms", "Shared CPU pass timings (ms)") : T("menu.profiling.shared_gpu_pass_timings_ms", "Shared GPU pass timings (ms)"));
		MenuUI::DetailText(Util::FeatureProfiling::sharedCoverage);
		std::vector<std::string> prefixes(view->sharedPrefixes.begin(), view->sharedPrefixes.end());
		rendered = renderSection(featurePrefix + "::shared", true,
					   [&](bool statistics) { return CollectFeatureTimingData(prefixes, cpuMode, statistics, TimingAttribution::Shared); }) ||
		           rendered;
	}
	return rendered;
}

bool ProfilingRenderer::RenderFeatureOverview()
{
	if (!globals::profiler || !globals::profiler->IsInitialized())
		return false;
	std::vector<std::string> activeFeatures;
	activeFeatures.reserve(featureTimingModes.size());
	for (const auto& [featurePrefix, featureMode] : featureTimingModes) {
		if (featureMode != FeatureTimingMode::Off)
			activeFeatures.push_back(featurePrefix);
	}

	if (activeFeatures.empty())
		return false;

	std::sort(activeFeatures.begin(), activeFeatures.end(), [](const auto& lhs, const auto& rhs) {
		return lhs < rhs;
	});

	ImGui::SeparatorText(T("menu.profiling.feature_profiling_overview", "Feature Profiling Overview"));

	if (ImGui::BeginTable("##FeatureProfilingOverview", 3,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg)) {
		const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
		ImGui::TableSetupColumn(T("menu.profiling.feature", "Feature"), ImGuiTableColumnFlags_WidthFixed, GetTextColumnWidth("Feature", activeFeatures));
		ImGui::TableSetupColumn(T("menu.profiling.gpu", "GPU"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(T("menu.profiling.cpu", "CPU"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();

		for (const auto& featurePrefix : activeFeatures) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(featurePrefix.c_str());

			if (const auto* view = Util::FeatureProfiling::Find(featurePrefix))
				Util::AddTooltip(view->coverage);

			for (const auto mode : { FeatureTimingMode::GPU, FeatureTimingMode::CPU }) {
				ImGui::TableNextColumn();
				ImGui::PushID((featurePrefix + (mode == FeatureTimingMode::CPU ? "::CPU" : "::GPU")).c_str());
				const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
				RenderFeatureTimingData(featurePrefix, mode, false);
			}
		}
	}

	ImGui::Spacing();
	return true;
}

bool ProfilingRenderer::CanProfileFeature(std::string_view a_feature)
{
	return !Util::FeatureProfiling::IsExcluded(a_feature) &&
	       (Util::FeatureProfiling::Find(a_feature) || HasFeatureTimers(std::string(a_feature)));
}

const char* ProfilingRenderer::GetProfilingDisabledReason()
{
	if (!globals::profiler || !globals::profiler->IsInitialized())
		return T("menu.profiling.unavailable", "Profiler is unavailable.");
	if (!globals::profiler->IsUserEnabled())
		return T("menu.profiling.enable_in_sidebar", "Profiling is off. Enable it on the main Profiling page in the sidebar.");
	return nullptr;
}

bool ProfilingRenderer::HasFeatureTimers(const std::string& featurePrefix)
{
	if (!globals::profiler || !globals::profiler->IsInitialized() || Util::FeatureProfiling::IsExcluded(featurePrefix))
		return false;

	const auto* view = Util::FeatureProfiling::Find(featurePrefix);
	for (const auto& result : globals::profiler->GetResults(false)) {
		if (!result.valid || !HasAnyLiveTimingMode(result))
			continue;
		const bool hasOwnedSamples = !view ||
		                             (view->HasOwnedTimings(false) && HasLiveTimingMode(result, false)) ||
		                             (view->HasOwnedTimings(true) && HasLiveTimingMode(result, true));
		if (hasOwnedSamples && (view ? Util::FeatureProfiling::MatchesOwned(*view, result.name) : Util::FeatureProfiling::Matches(result.name, featurePrefix)))
			return true;
		if (view && std::ranges::any_of(view->sharedPrefixes, [&](auto prefix) { return Util::FeatureProfiling::Matches(result.name, prefix); }))
			return true;
	}
	return false;
}

bool ProfilingRenderer::RenderEnabledControl()
{
	const bool available = globals::profiler && globals::profiler->IsInitialized();
	bool enabled = available && globals::profiler->IsUserEnabled();
	const auto disabled = Util::DisableGuard(!available || PerformanceTuningRenderer::HasActiveMeasurements());
	if (Util::Widgets::Checkbox(T("menu.profiling.enabled", "Enabled"), &enabled)) {
		globals::profiler->SetUserEnabled(enabled);
		timeSinceLastUpdate = kStatsRefreshSeconds;
	}
	Util::AddTooltip(available ? T("menu.profiling.enable_runtime_cpu_and_gpu_profiling_no_restart_required", "Enable runtime CPU and GPU profiling. No restart required.") : T("menu.profiling.unavailable", "Profiler is unavailable."), ImGuiHoveredFlags_AllowWhenDisabled);
	return enabled;
}

void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle, bool showEnabledToggle)
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (showTable)
		NvPerf::DrawSettings();
#endif
	if (!globals::profiler || !globals::profiler->IsInitialized()) {
		ImGui::TextDisabled(T("menu.profiling.profiler_is_unavailable", "Profiler is unavailable."));
		return;
	}
	auto& profiler = (*globals::profiler);
	if (statisticsHistoryEpoch != profiler.GetHistoryEpoch()) {
		statisticsHistoryEpoch = profiler.GetHistoryEpoch();
		timeSinceLastUpdate = kStatsRefreshSeconds;
	}
	const bool fullProfilerPage = showTable || showModeToggle;

	if (fullProfilerPage) {
		const bool profilingEnabled = showEnabledToggle ? RenderEnabledControl() : profiler.IsUserEnabled();
		if (showEnabledToggle)
			ImGui::Separator();
		if (!profilingEnabled) {
			ImGui::TextDisabled(T("menu.profiling.profiling_is_off_turn_on_enabled_to_capture_timings", "Profiling is off. Turn on Enabled to capture timings."));
			return;
		}
	} else if (const auto* reason = GetProfilingDisabledReason()) {
		ImGui::TextDisabled("%s", reason);
		return;
	}

	profiler.RequestCapture();

	if (fullProfilerPage)
		globals::features::performanceOverlay.DrawRenderTargetProfiling();

	bool cpuMode = (timingMode == TimingMode::CPU);
	if (showModeToggle) {
		RenderTimingModeToggle();
		cpuMode = (timingMode == TimingMode::CPU);
		ImGui::TextDisabled(T("menu.profiling.self_time_profiled_descendants_excluded", "Self time (profiled descendants excluded)"));
		ImGui::Separator();
	}

	float currentTime = static_cast<float>(ImGui::GetTime());
	float deltaTime = currentTime - lastFrameTime;
	lastFrameTime = currentTime;
	timeSinceLastUpdate += deltaTime;

	if (timeSinceLastUpdate >= kStatsRefreshSeconds) {
		timeSinceLastUpdate = 0.0f;

		cachedGroups.clear();
		cachedTotalAvgMs = 0.0f;
		cachedTotalP95Ms = 0.0f;
		cachedTotalP99Ms = 0.0f;
		cachedMaxAvgMs = 0.0f;
		cachedMaxP95Ms = 0.0f;
		cachedMaxP99Ms = 0.0f;
		std::unordered_map<std::string, size_t> groupIndex;
		std::unordered_map<std::string, DisplayTimingSampleAccumulator> groupSampleTotals;
		DisplayTimingSampleAccumulator totalSamples;

		for (const auto& result : profiler.GetResults(false)) {
			if (!result.valid || !HasLiveTimingMode(result, cpuMode))
				continue;

			std::array<float, kDisplayedRollingFrameCount> samples{};
			const uint32_t sampleCount = CollectDisplayTimingSamples(result, cpuMode, samples);
			if (sampleCount == 0)
				continue;

			const auto stats = ComputeDisplayTimingStats(samples, sampleCount);
			float avg = stats.avgMs;
			float p95 = stats.p95Ms;
			float p99 = stats.p99Ms;
			totalSamples.Add(samples, sampleCount);

			auto pos = result.name.find("::");
			if (pos != std::string::npos) {
				std::string groupName = result.name.substr(0, pos);
				std::string passLabel = result.name.substr(pos + 2);

				auto it = groupIndex.find(groupName);
				if (it == groupIndex.end()) {
					groupIndex[groupName] = cachedGroups.size();
					cachedGroups.push_back({ groupName, 0, 0, 0 });
				}

				auto& group = cachedGroups[groupIndex[groupName]];
				group.passes.push_back({ passLabel, avg, p95, p99 });
				groupSampleTotals[groupName].Add(samples, sampleCount);
			} else {
				groupIndex[result.name] = cachedGroups.size();
				cachedGroups.push_back({ result.name, avg, p95, p99 });
			}
		}

		const auto totalStats = totalSamples.GetStats();
		cachedTotalAvgMs = totalStats.avgMs;
		cachedTotalP95Ms = totalStats.p95Ms;
		cachedTotalP99Ms = totalStats.p99Ms;

		for (auto& group : cachedGroups) {
			if (!group.passes.empty()) {
				if (auto it = groupSampleTotals.find(group.name); it != groupSampleTotals.end()) {
					const auto groupStats = it->second.GetStats();
					group.totalAvgMs = groupStats.avgMs;
					group.totalP95Ms = groupStats.p95Ms;
					group.totalP99Ms = groupStats.p99Ms;
				}
			}

			cachedMaxAvgMs = std::max(cachedMaxAvgMs, group.totalAvgMs);
			cachedMaxP95Ms = std::max(cachedMaxP95Ms, group.totalP95Ms);
			cachedMaxP99Ms = std::max(cachedMaxP99Ms, group.totalP99Ms);
		}
	}

	const bool renderedFeatureOverview = fullProfilerPage && RenderFeatureOverview();
	if (cachedGroups.empty()) {
		if (!renderedFeatureOverview)
			ImGui::TextDisabled(T("menu.profiling.no_timing_data_available_enter_game_world", "No timing data available (enter game world)"));
		return;
	}

	if (renderedFeatureOverview)
		ImGui::SeparatorText(T("menu.profiling.all_timings", "All Timings"));
	RenderGraph();

	if (showTable) {
		float availHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
		std::vector<std::string> passLabels;
		passLabels.reserve(cachedGroups.size());
		for (const auto& group : cachedGroups) {
			passLabels.push_back(group.name);
			for (const auto& pass : group.passes)
				passLabels.push_back(pass.label);
		}
		const float passColumnWidth = GetTextColumnWidth("Pass", passLabels, GetColorMarkerExtraWidth() + ImGui::GetTreeNodeToLabelSpacing() + ImGui::GetStyle().IndentSpacing);

		if (ImGui::BeginTable("##Profiler", 5,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY,
				ImVec2(0.0f, availHeight))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			SetupTimingTableColumns(passColumnWidth, true);
			ImGui::TableHeadersRow();

			for (const auto& group : cachedGroups) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				if (group.passes.empty()) {
					RenderColorMarker(GetGroupColor(group.name));
					ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalAvgMs, cachedMaxAvgMs);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP95Ms, cachedMaxP95Ms);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP99Ms, cachedMaxP99Ms);
					ImGui::TableNextColumn();
					if (cachedTotalAvgMs > 0.0f)
						TextHeat("%5.1f", (group.totalAvgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
				} else {
					const ImU32 groupColor = GetGroupColor(group.name);
					RenderColorMarker(groupColor);
					bool open = ImGui::TreeNodeEx(group.name.c_str(), 0);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalAvgMs, cachedMaxAvgMs);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP95Ms, cachedMaxP95Ms);
					ImGui::TableNextColumn();
					TextHeat("%.3f", group.totalP99Ms, cachedMaxP99Ms);
					ImGui::TableNextColumn();
					if (cachedTotalAvgMs > 0.0f)
						TextHeat("%5.1f", (group.totalAvgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
					if (open) {
						for (const auto& pass : group.passes) {
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							RenderColorMarker(groupColor);
							ImGui::TreeNodeEx(pass.label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.avgMs, cachedMaxAvgMs);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.p95Ms, cachedMaxP95Ms);
							ImGui::TableNextColumn();
							TextHeat("%.3f", pass.p99Ms, cachedMaxP99Ms);
							ImGui::TableNextColumn();
							if (cachedTotalAvgMs > 0.0f)
								TextHeat("%5.1f", (pass.avgMs / cachedTotalAvgMs) * 100.0f, 100.0f);
						}
						ImGui::TreePop();
					}
				}
			}
			ImGui::EndTable();
		}
	}
}

void ProfilingRenderer::RenderFeatureTimers(const std::string& featurePrefix)
{
	if (!CanProfileFeature(featurePrefix))
		return;
	if (const auto* reason = GetProfilingDisabledReason()) {
		ImGui::TextDisabled("%s", reason);
		return;
	}
	const auto* view = Util::FeatureProfiling::Find(featurePrefix);
	if (view)
		MenuUI::DetailNote(view->coverage);
	auto& profiler = (*globals::profiler);
	auto& featureMode = featureTimingModes[featurePrefix];

	int mode = static_cast<int>(featureMode);
	const MenuUI::Choice choices[] = {
		{ "off", "Off", "Stop live profiling", "No feature timing capture." },
		{ "gpu", "GPU", "Graphics-card timings", "Show instrumented graphics-card pass timings." },
		{ "cpu", "CPU", "Processor timings", "Show CPU updates and rendering submission timings." }
	};
	const int chosen = MenuUI::ChoiceCards("FeatureTimingMode", mode, choices);
	if (chosen >= 0)
		mode = chosen;

	mode = std::clamp(mode, static_cast<int>(FeatureTimingMode::Off), static_cast<int>(FeatureTimingMode::CPU));
	if (chosen >= 0 && featureMode != static_cast<FeatureTimingMode>(mode)) {
		featureMode = static_cast<FeatureTimingMode>(mode);
		if (auto it = featureGraphs.find(featurePrefix); it != featureGraphs.end()) {
			it->second.gpuGate.Reset();
			it->second.cpuGate.Reset();
		}
	}

	if (featureMode == FeatureTimingMode::Off) {
		ImGui::TextDisabled(T("menu.profiling.feature_profiling_is_off", "Feature profiling is off."));
		return;
	}

	profiler.RequestCapture(featurePrefix == "NeuralRendering" ?
								(featureMode == FeatureTimingMode::GPU ? Profiler::CaptureMode::GPU : Profiler::CaptureMode::CPU) :
								Profiler::CaptureMode::Both);
	RenderFeatureTimingData(featurePrefix, featureMode, true);
}

void ProfilingRenderer::RenderFeaturePerformanceSummary(
	const std::string& featurePrefix,
	const PerformanceTimingHighlight* highlight)
{
	RenderFeaturePerformanceSummary(std::vector<std::string>{ featurePrefix }, highlight);
}

void ProfilingRenderer::RenderFeaturePerformanceSummary(
	const std::vector<std::string>& featurePrefixes,
	const PerformanceTimingHighlight* highlight)
{
	if (!globals::profiler) {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.performance_tuning.profiling.unavailable",
				"Profiling is unavailable."));
		return;
	}

	if (featurePrefixes.empty()) {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.performance_tuning.profiling.no_feature",
				"No feature is selected."));
		return;
	}

	const auto& profiler = *globals::profiler;
	if (!profiler.IsDetailedCaptureActive()) {
		const bool wholeFrameOnly =
			profiler.IsWholeFrameCaptureActive();
		ImGui::TextDisabled(
			"%s",
			wholeFrameOnly ?
				T(
					"menu.performance_tuning.profiling.capture_mode.whole_frame",
					"Capture: Whole frame only") :
				T(
					"menu.performance_tuning.profiling.capture_mode.pending",
					"Capture: Pending"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				wholeFrameOnly ?
					T(
						"menu.performance_tuning.profiling.capture_mode.whole_frame_tooltip",
						"Named pass scopes are paused during cost measurement to keep measurement overhead constant. GPU and CPU whole-frame boundaries remain separate supporting metrics.") :
					T(
						"menu.performance_tuning.profiling.capture_mode.pending_tooltip",
						"Detailed pass capture will begin at the next successful game-frame Present."));
			ImGui::TextUnformatted(
				T(
					"menu.performance_tuning.profiling.pre_fg_explanation",
					"Game-frame Total and FPS use the direct pre-frame-generation Present-to-Present interval. Profiled pass timings are never added to Total."));
		}
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.performance_tuning.profiling.detailed_paused",
				"Detailed pass timing is paused."));
		return;
	}

	ImGui::TextDisabled(
		"%s",
		T(
			"menu.performance_tuning.profiling.capture_mode.detailed",
			"Capture: Detailed passes"));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(
				"menu.performance_tuning.profiling.capture_mode.detailed_tooltip",
				"Named GPU and CPU pass scopes are active for the selected feature."));
		ImGui::TextUnformatted(
			T(
				"menu.performance_tuning.profiling.pre_fg_explanation",
				"Game-frame Total and FPS use the direct pre-frame-generation Present-to-Present interval. Profiled pass timings are never added to Total."));
	}

	const auto gpuData = CollectFeatureTimingData(featurePrefixes, false);
	const auto cpuData = CollectFeatureTimingData(featurePrefixes, true);
	auto& graphState = featureGraphs[BuildTimingPrefixKey(featurePrefixes)];

	ImGui::TextUnformatted(
		T(
			"menu.performance_tuning.profiling.gpu",
			"GPU"));
	ImGui::PushID("PerformanceSummaryGPU");
	if (RenderFeatureTimingGraph(featurePrefixes.front(), gpuData, graphState.gpuGraph, graphState.gpuGate, 82)) {
		RenderFeatureTimingStats(gpuData.totalAvg, gpuData.totalP95, gpuData.totalP99, highlight ? highlight->featureGpuDirection : 0);
	} else {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.performance_tuning.profiling.no_gpu_data",
				"No GPU pass timing data."));
	}
	ImGui::PopID();

	ImGui::Spacing();
	ImGui::TextUnformatted(
		T(
			"menu.performance_tuning.profiling.cpu",
			"CPU"));
	ImGui::PushID("PerformanceSummaryCPU");
	if (RenderFeatureTimingGraph(featurePrefixes.front(), cpuData, graphState.cpuGraph, graphState.cpuGate, 82)) {
		RenderFeatureTimingStats(cpuData.totalAvg, cpuData.totalP95, cpuData.totalP99, highlight ? highlight->featureCpuDirection : 0);
	} else {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.performance_tuning.profiling.no_cpu_data",
				"No CPU pass timing data."));
	}
	ImGui::PopID();
}

ProfilingRenderer::PerformanceTimingSummary ProfilingRenderer::CapturePerformanceTimingSummary(
	const std::vector<std::string>& featurePrefixes,
	Profiler::CaptureMode captureMode)
{
	PerformanceTimingSummary summary;
	if (!globals::profiler)
		return summary;

	auto& profiler = (*globals::profiler);
	const bool requestCapture = captureMode != Profiler::CaptureMode::None;
	if (requestCapture && !profiler.IsUserEnabled())
		profiler.SetUserEnabled(true);
	if (requestCapture)
		profiler.RequestCapture(captureMode);

	const bool includeDetailedTimings =
		captureMode == Profiler::CaptureMode::DetailedPasses &&
		profiler.GetCaptureModeLimit() ==
			Profiler::CaptureMode::DetailedPasses &&
		profiler.IsDetailedCaptureActive();
	if (includeDetailedTimings) {
		for (const auto& featurePrefix : featurePrefixes) {
			summary.features.try_emplace(featurePrefix);
		}
	}

	if (includeDetailedTimings) {
		for (const auto& prefix : featurePrefixes) {
			const auto gpu = CollectFeatureTimingData(prefix, false, false);
			const auto cpu = CollectFeatureTimingData(prefix, true, false);
			auto& totals = summary.features[prefix];
			totals.gpuAvgMs = gpu.totalAvg;
			totals.cpuAvgMs = cpu.totalAvg;
			totals.hasGpu = !gpu.entries.empty();
			totals.hasCpu = !cpu.entries.empty();
		}
	}

	summary.presentIntervalSampleId = profiler.GetPresentIntervalSampleId();
	summary.wholeFrameSampleId = profiler.GetWholeFrameSampleId();
	summary.wholeFramePresentIntervalSampleId = profiler.GetWholeFramePresentIntervalSampleId();
	summary.wholeFrameGpuSampleId = profiler.GetWholeFrameGpuSampleId();
	summary.wholeFrameCpuSampleId = profiler.GetWholeFrameCpuSampleId();
	summary.presentDiscontinuityEpoch = profiler.GetPresentDiscontinuityEpoch();
	summary.skippedWholeFrameCaptureCount = profiler.GetSkippedWholeFrameCaptureCount();
	if (profiler.HasWholeFrameGpuTime()) {
		summary.wholeFrameGpuMs = profiler.GetWholeFrameGpuTimeAverageMs(kDisplayedRollingFrameCount);
		summary.hasWholeFrameGpu = IsPositiveFinite(summary.wholeFrameGpuMs);
	}
	if (profiler.HasWholeFrameCpuTime()) {
		summary.wholeFrameCpuMs = profiler.GetWholeFrameCpuTimeAverageMs(kDisplayedRollingFrameCount);
		summary.hasWholeFrameCpu = IsPositiveFinite(summary.wholeFrameCpuMs);
	}
	if (profiler.HasPresentInterval()) {
		summary.presentIntervalMs = profiler.GetPresentIntervalAverageMs(kDisplayedRollingFrameCount);
		summary.hasPresentInterval = IsPositiveFinite(summary.presentIntervalMs);
		if (summary.hasPresentInterval)
			summary.fps = 1000.0f / summary.presentIntervalMs;
	}

	if (profiler.HasLatestWholeFrameGpuSample()) {
		summary.wholeFrameGpuSampleMs = profiler.GetWholeFrameGpuTimeMs();
		summary.hasWholeFrameGpuSample = IsPositiveFinite(summary.wholeFrameGpuSampleMs);
	}
	if (profiler.HasLatestWholeFrameCpuSample()) {
		summary.wholeFrameCpuSampleMs = profiler.GetWholeFrameCpuTimeMs();
		summary.hasWholeFrameCpuSample = IsPositiveFinite(summary.wholeFrameCpuSampleMs);
	}
	if (profiler.HasLatestPresentIntervalSample()) {
		summary.presentIntervalSampleMs = profiler.GetPresentIntervalMs();
		summary.hasPresentIntervalSample = IsPositiveFinite(summary.presentIntervalSampleMs);
		summary.presentIntervalSynced =
			summary.hasPresentIntervalSample && profiler.WasLatestPresentIntervalSynced();
	}

	summary.valid =
		summary.hasPresentInterval ||
		summary.hasWholeFrameGpu ||
		summary.hasWholeFrameCpu;
	return summary;
}
