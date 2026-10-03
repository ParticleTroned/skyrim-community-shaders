#include "ProfilingRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Globals.h"
#include "I18n/I18n.h"
#include "Util.h"
#include "Utils/UI.h"

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

static int ComputeGraphLegendWidth(int totalWidth, int minGraphWidth, float widthFraction, int minLegendWidth, int maxLegendWidth)
{
	const int reservedGraphWidth = std::min(minGraphWidth, totalWidth);
	const int availableLegendWidth = std::max(0, totalWidth - reservedGraphWidth);
	if (availableLegendWidth <= 0)
		return 0;

	const int desiredLegendWidth = std::clamp(static_cast<int>(totalWidth * widthFraction), minLegendWidth, maxLegendWidth);
	return std::min(desiredLegendWidth, availableLegendWidth);
}

static bool HasTimingMode(const Profiler::TimerResult& result, bool cpuMode)
{
	return cpuMode ? result.hasCpu : result.hasGpu;
}

static constexpr uint32_t kDisplayedRollingFrameCount = 60;

static bool IsPositiveFinite(float value)
{
	return std::isfinite(value) && value > 0.0f;
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

struct DisplayTimingStats
{
	float timeMs = 0.0f;
	float avgMs = 0.0f;
	float p95Ms = 0.0f;
	float p99Ms = 0.0f;
};

static float GetSortedPercentile(const std::array<float, kDisplayedRollingFrameCount>& samples, uint32_t sampleCount, float percentile)
{
	if (sampleCount == 0)
		return 0.0f;

	const float idx = (percentile / 100.0f) * static_cast<float>(sampleCount - 1);
	const uint32_t lo = static_cast<uint32_t>(idx);
	const uint32_t hi = std::min(lo + 1, sampleCount - 1);
	const float frac = idx - static_cast<float>(lo);
	return samples[lo] * (1.0f - frac) + samples[hi] * frac;
}

static uint32_t CollectDisplayTimingSamples(const Profiler::TimerResult& result, bool cpuMode, std::array<float, kDisplayedRollingFrameCount>& samples)
{
	samples.fill(0.0f);

	const uint32_t historyCount = cpuMode ? result.cpuHistoryCount : result.historyCount;
	const uint32_t sampleCount = std::min(historyCount, kDisplayedRollingFrameCount);
	if (sampleCount == 0)
		return 0;

	const uint32_t firstSample = historyCount - sampleCount;
	for (uint32_t i = 0; i < sampleCount; ++i)
		samples[i] = cpuMode ?
		                 result.GetCpuHistorySample(firstSample + i) :
		                 result.GetHistorySample(firstSample + i);

	return sampleCount;
}

static DisplayTimingStats ComputeDisplayTimingStats(std::array<float, kDisplayedRollingFrameCount> samples, uint32_t sampleCount)
{
	DisplayTimingStats stats;
	if (sampleCount == 0)
		return stats;

	float sum = 0.0f;
	for (uint32_t i = 0; i < sampleCount; ++i)
		sum += samples[i];

	stats.avgMs = sum / static_cast<float>(sampleCount);
	std::sort(samples.begin(), samples.begin() + sampleCount);
	stats.p95Ms = GetSortedPercentile(samples, sampleCount, 95.0f);
	stats.p99Ms = GetSortedPercentile(samples, sampleCount, 99.0f);
	stats.timeMs = stats.avgMs;
	return stats;
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

	[[nodiscard]] DisplayTimingStats GetStats() const
	{
		if (sampleCount == 0)
			return {};

		std::array<float, kDisplayedRollingFrameCount> compactSamples{};
		const uint32_t sampleOffset = kDisplayedRollingFrameCount - sampleCount;
		for (uint32_t i = 0; i < sampleCount; ++i)
			compactSamples[i] = samples[sampleOffset + i];

		return ComputeDisplayTimingStats(compactSamples, sampleCount);
	}

	std::array<float, kDisplayedRollingFrameCount> samples{};
	uint32_t sampleCount = 0;
};

static bool TryMatchTimingPrefix(std::string_view timerName, std::string_view prefix, bool compactLabel, std::string& label)
{
	if (timerName == prefix) {
		label.assign(timerName.data(), timerName.size());
		return true;
	}

	if (!timerName.starts_with(prefix) || timerName.size() <= prefix.size() + 2 || timerName[prefix.size()] != ':' || timerName[prefix.size() + 1] != ':')
		return false;

	if (compactLabel) {
		const auto compact = timerName.substr(prefix.size() + 2);
		label.assign(compact.data(), compact.size());
	} else {
		label.assign(timerName.data(), timerName.size());
	}
	return true;
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

static std::string GetTimingRootName(const std::string& timerName)
{
	const auto pos = timerName.find("::");
	return pos == std::string::npos ? timerName : timerName.substr(0, pos);
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

void ProfilingRenderer::RenderGraph()
{
	auto& profiler = (*globals::profiler);
	const auto& results = profiler.GetResults();
	bool cpuMode = (timingMode == TimingMode::CPU);

	if (results.empty())
		return;

	std::vector<legit::ProfilerTask> tasks;

	double accumulated = 0.0;
	for (const auto& result : results) {
		if (!result.valid || !HasTimingMode(result, cpuMode))
			continue;

		std::array<float, kDisplayedRollingFrameCount> samples{};
		const uint32_t sampleCount = CollectDisplayTimingSamples(result, cpuMode, samples);
		if (sampleCount == 0)
			continue;
		const auto stats = ComputeDisplayTimingStats(samples, sampleCount);
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

	gpuGraph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = gpuGraph.GetPeakFrameTime() * 1.2f;
	if (maxFrameTimeSec < 0.0001f)
		maxFrameTimeSec = 0.0001f;

	const int totalWidth = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
	const int legendWidth = ComputeGraphLegendWidth(totalWidth, 100, 0.42f, 280, 420);
	const int graphWidth = std::max(1, totalWidth - legendWidth);
	int graphHeight = 180;

	gpuGraph.RenderTimings(graphWidth, legendWidth, graphHeight, 0, maxFrameTimeSec);

	ImGui::Spacing();
}

ProfilingRenderer::FeatureTimingData ProfilingRenderer::CollectFeatureTimingData(const std::string& featurePrefix, bool cpuMode)
{
	return CollectFeatureTimingData(std::vector<std::string>{ featurePrefix }, cpuMode);
}

ProfilingRenderer::FeatureTimingData ProfilingRenderer::CollectFeatureTimingData(const std::vector<std::string>& featurePrefixes, bool cpuMode)
{
	const auto& results = globals::profiler->GetResults();

	FeatureTimingData data;
	DisplayTimingSampleAccumulator totalSamples;
	const bool compactLabel = featurePrefixes.size() == 1;
	for (const auto& r : results) {
		if (!r.valid || !HasTimingMode(r, cpuMode))
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
		const auto stats = ComputeDisplayTimingStats(samples, sampleCount);
		float timeMs = stats.timeMs;
		float avg = stats.avgMs;
		float p95 = stats.p95Ms;
		float p99 = stats.p99Ms;
		data.entries.push_back({ label, r.name, timeMs, avg, p95, p99 });
		data.maxAvg = std::max(data.maxAvg, avg);
		data.maxP95 = std::max(data.maxP95, p95);
		data.maxP99 = std::max(data.maxP99, p99);
		totalSamples.Add(samples, sampleCount);
	}

	const auto totalStats = totalSamples.GetStats();
	data.totalAvg = totalStats.avgMs;
	data.totalP95 = totalStats.p95Ms;
	data.totalP99 = totalStats.p99Ms;
	data.maxAvg = std::max(data.maxAvg, data.totalAvg);
	data.maxP95 = std::max(data.maxP95, data.totalP95);
	data.maxP99 = std::max(data.maxP99, data.totalP99);

	return data;
}

bool ProfilingRenderer::RenderFeatureTimingGraph([[maybe_unused]] const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, int graphHeight)
{
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

	graph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = graph.GetPeakFrameTime() * 1.2f;
	if (maxFrameTimeSec < 0.00001f)
		maxFrameTimeSec = 0.00001f;

	const int totalWidth = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
	const int legendWidth = totalWidth >= 180 ? ComputeGraphLegendWidth(totalWidth, 80, 0.50f, 140, 300) : 0;
	const int graphWidth = std::max(1, totalWidth - legendWidth);

	graph.RenderTimings(graphWidth, legendWidth, graphHeight, 0, maxFrameTimeSec);
	return true;
}

bool ProfilingRenderer::RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable)
{
	bool cpuMode = featureMode == FeatureTimingMode::CPU;
	const auto data = CollectFeatureTimingData(featurePrefix, cpuMode);

	if (data.entries.empty()) {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.profiling.no_timing_data",
				"No timing data"));
		return false;
	}

	ImGui::PushID(featurePrefix.c_str());
	auto& state = featureGraphs[featurePrefix];
	auto& graph = cpuMode ? state.cpuGraph : state.gpuGraph;
	if (RenderFeatureTimingGraph(featurePrefix, data, graph, 100))
		ImGui::Spacing();

	if (showTable && ImGui::BeginTable("##FeatureTimers", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
		std::vector<std::string> passLabels;
		passLabels.reserve(data.entries.size() + 1);
		for (const auto& e : data.entries)
			passLabels.push_back(e.label);
		const char* totalLabel =
			T("menu.profiling.total", "Total");
		const char* passLabel =
			T("menu.profiling.pass", "Pass");
		passLabels.emplace_back(totalLabel);
		ImGui::TableSetupColumn(passLabel, ImGuiTableColumnFlags_WidthFixed, GetTextColumnWidth(passLabel, passLabels, GetColorMarkerExtraWidth()));
		ImGui::TableSetupColumn(T("menu.profiling.avg", "Avg"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableSetupColumn(T("menu.profiling.p95", "P95"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableSetupColumn(T("menu.profiling.p99", "P99"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
		ImGui::TableHeadersRow();

		for (const auto& e : data.entries) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			RenderColorMarker(GetGroupColor(e.colorKey));
			ImGui::TextUnformatted(e.label.c_str());
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.avgMs, data.maxAvg);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p95Ms, data.maxP95);
			ImGui::TableNextColumn();
			TextHeat("%.3f", e.p99Ms, data.maxP99);
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextColored(
			ImVec4(1.0f, 1.0f, 0.6f, 1.0f),
			"%s",
			totalLabel);
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", data.totalAvg);
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", data.totalP95);
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%.3f", data.totalP99);

		ImGui::EndTable();
	}

	ImGui::PopID();
	return true;
}

bool ProfilingRenderer::RenderFeatureOverview()
{
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

	ImGui::SeparatorText(
		T(
			"menu.profiling.feature_overview",
			"Feature Profiling Overview"));

	if (ImGui::BeginTable("##FeatureProfilingOverview", 3,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg)) {
		const char* featureLabel =
			T("menu.profiling.feature", "Feature");
		ImGui::TableSetupColumn(featureLabel, ImGuiTableColumnFlags_WidthFixed, GetTextColumnWidth(featureLabel, activeFeatures));
		ImGui::TableSetupColumn(T("menu.profiling.gpu", "GPU"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(T("menu.profiling.cpu", "CPU"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();

		for (const auto& featurePrefix : activeFeatures) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(featurePrefix.c_str());

			const auto gpuData = CollectFeatureTimingData(featurePrefix, false);
			const auto cpuData = CollectFeatureTimingData(featurePrefix, true);
			auto& state = featureGraphs[featurePrefix];

			ImGui::TableNextColumn();
			ImGui::PushID((featurePrefix + "::GPU").c_str());
			if (!RenderFeatureTimingGraph(featurePrefix, gpuData, state.gpuGraph, 85))
				ImGui::TextDisabled(
					"%s",
					T(
						"menu.profiling.no_gpu_timing_data",
						"No GPU timing data"));
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID((featurePrefix + "::CPU").c_str());
			if (!RenderFeatureTimingGraph(featurePrefix, cpuData, state.cpuGraph, 85))
				ImGui::TextDisabled(
					"%s",
					T(
						"menu.profiling.no_cpu_timing_data",
						"No CPU timing data"));
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();
	return true;
}

bool ProfilingRenderer::HasFeatureTimers(const std::string& featurePrefix)
{
	if (!globals::profiler)
		return false;

	const std::string prefix = featurePrefix + "::";
	for (const auto& result : globals::profiler->GetResults()) {
		if (result.valid && result.name.starts_with(prefix))
			return true;
	}

	return false;
}

void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle)
{
	auto& profiler = (*globals::profiler);
	const bool fullProfilerPage = showTable || showModeToggle;

	if (fullProfilerPage) {
		bool profilingEnabled = profiler.IsUserEnabled();
		ImGui::TextUnformatted(
			T(
				"menu.profiling.title",
				"Profiling"));
		ImGui::SameLine();
		if (ImGui::Checkbox(
			    T(
				    "menu.profiling.enable",
				    "Enable"),
			    &profilingEnabled)) {
			profiler.SetUserEnabled(profilingEnabled);
			timeSinceLastUpdate = 1.0f;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				T(
					"menu.profiling.enable_tooltip",
					"Runtime profiling capture. No restart required."));
			ImGui::TextUnformatted(
				T(
					"menu.profiling.enable_tooltip_off",
					"When off, profiling capture requests are ignored and no timestamp/query timing scopes are recorded."));
		}
		ImGui::Separator();

		if (!profilingEnabled) {
			ImGui::TextDisabled(
				"%s",
				T(
					"menu.profiling.off",
					"Profiling is off."));
			return;
		}
	} else if (!profiler.IsUserEnabled()) {
		profiler.SetUserEnabled(true);
	}

	profiler.RequestCapture(Profiler::CaptureMode::DetailedPasses);

	bool cpuMode = (timingMode == TimingMode::CPU);
	if (showModeToggle) {
		int mode = static_cast<int>(timingMode);
		ImGui::RadioButton(
			T(
				"menu.profiling.mode.gpu",
				"GPU"),
			&mode,
			0);
		ImGui::SameLine();
		ImGui::RadioButton(
			T(
				"menu.profiling.mode.cpu",
				"CPU"),
			&mode,
			1);
		if (static_cast<TimingMode>(mode) != timingMode) {
			timingMode = static_cast<TimingMode>(mode);
			timeSinceLastUpdate = 1.0f;
		}
		cpuMode = (timingMode == TimingMode::CPU);
		ImGui::Separator();
	}

	float currentTime = static_cast<float>(ImGui::GetTime());
	float deltaTime = currentTime - lastFrameTime;
	lastFrameTime = currentTime;
	timeSinceLastUpdate += deltaTime;

	if (timeSinceLastUpdate >= 1.0f) {
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

		for (const auto& result : profiler.GetResults()) {
			if (!result.valid || !HasTimingMode(result, cpuMode))
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
			ImGui::TextDisabled(
				"%s",
				T(
					"menu.profiling.no_timing_data_world",
					"No timing data available (enter game world)"));
		return;
	}

	if (renderedFeatureOverview)
		ImGui::SeparatorText(
			T(
				"menu.profiling.all_timings",
				"All Timings"));
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
		const char* passLabel =
			T("menu.profiling.pass", "Pass");
		const float passColumnWidth = GetTextColumnWidth(passLabel, passLabels, GetColorMarkerExtraWidth() + ImGui::GetTreeNodeToLabelSpacing() + ImGui::GetStyle().IndentSpacing);

		if (ImGui::BeginTable("##Profiler", 5,
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, availHeight))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(passLabel, ImGuiTableColumnFlags_WidthFixed, passColumnWidth);
			ImGui::TableSetupColumn(T("menu.profiling.avg", "Avg"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn(T("menu.profiling.p95", "P95"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn(T("menu.profiling.p99", "P99"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn(T("menu.profiling.percent", "%"), ImGuiTableColumnFlags_WidthFixed, 45.0f);
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
	auto& profiler = (*globals::profiler);
	auto& featureMode = featureTimingModes[featurePrefix];

	int mode = static_cast<int>(featureMode);
	const int previousMode = mode;
	ImGui::RadioButton(
		T(
			"menu.profiling.feature_mode.off",
			"Off"),
		&mode,
		static_cast<int>(FeatureTimingMode::Off));
	ImGui::SameLine();
	ImGui::RadioButton(
		T(
			"menu.profiling.feature_mode.gpu",
			"GPU"),
		&mode,
		static_cast<int>(FeatureTimingMode::GPU));
	ImGui::SameLine();
	ImGui::RadioButton(
		T(
			"menu.profiling.feature_mode.cpu",
			"CPU"),
		&mode,
		static_cast<int>(FeatureTimingMode::CPU));

	mode = std::clamp(mode, static_cast<int>(FeatureTimingMode::Off), static_cast<int>(FeatureTimingMode::CPU));
	if (mode != previousMode) {
		featureMode = static_cast<FeatureTimingMode>(mode);
		if (featureMode != FeatureTimingMode::Off)
			profiler.SetUserEnabled(true);
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(
				"menu.profiling.feature_mode.off_tooltip",
				"Off: do not request profiling capture for this feature."));
		ImGui::TextUnformatted(
			T(
				"menu.profiling.feature_mode.active_tooltip",
				"GPU/CPU: enable runtime profiling and show this feature's timing in the selected mode."));
		ImGui::TextUnformatted(
			T(
				"menu.profiling.feature_mode.restart_tooltip",
				"No restart required."));
	}

	if (featureMode == FeatureTimingMode::Off) {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.profiling.feature_mode.feature_off",
				"Feature profiling is off."));
		return;
	}

	if (!profiler.IsUserEnabled()) {
		ImGui::TextDisabled(
			"%s",
			T(
				"menu.profiling.feature_mode.runtime_off",
				"Runtime profiling is off."));
		return;
	}

	profiler.RequestCapture(Profiler::CaptureMode::DetailedPasses);
	RenderFeatureTimingData(featurePrefix, featureMode, true);
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

	struct TimingBucket
	{
		float gpuExactMs = 0.0f;
		float cpuExactMs = 0.0f;
		float gpuChildMs = 0.0f;
		float cpuChildMs = 0.0f;
		bool hasGpuExact = false;
		bool hasCpuExact = false;
		bool hasGpuChild = false;
		bool hasCpuChild = false;
	};

	auto isRequestedFeatureRoot = [&](const std::string& rootName) {
		if (featurePrefixes.empty())
			return true;

		return std::ranges::find(featurePrefixes, rootName) != featurePrefixes.end();
	};

	std::unordered_map<std::string, TimingBucket> timingBuckets;
	if (includeDetailedTimings) {
		for (const auto& result : profiler.GetResults()) {
			if (!result.valid)
				continue;

			const std::string rootName = GetTimingRootName(result.name);
			if (!isRequestedFeatureRoot(rootName))
				continue;

			auto& bucket = timingBuckets[rootName];
			const bool rootTimer = result.name == rootName;
			if (HasTimingMode(result, false)) {
				DisplayTimingStats stats;
				if (TryGetDisplayTimingStats(result, false, stats)) {
					if (rootTimer) {
						bucket.gpuExactMs += stats.avgMs;
						bucket.hasGpuExact = true;
					} else {
						bucket.gpuChildMs += stats.avgMs;
						bucket.hasGpuChild = true;
					}
				}
			}

			if (HasTimingMode(result, true)) {
				DisplayTimingStats stats;
				if (TryGetDisplayTimingStats(result, true, stats)) {
					if (rootTimer) {
						bucket.cpuExactMs += stats.avgMs;
						bucket.hasCpuExact = true;
					} else {
						bucket.cpuChildMs += stats.avgMs;
						bucket.hasCpuChild = true;
					}
				}
			}
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

	for (const auto& [rootName, bucket] : timingBuckets) {
		const float gpuMs = bucket.hasGpuExact ? bucket.gpuExactMs : bucket.gpuChildMs;
		const float cpuMs = bucket.hasCpuExact ? bucket.cpuExactMs : bucket.cpuChildMs;
		const bool hasGpu = bucket.hasGpuExact || bucket.hasGpuChild;
		const bool hasCpu = bucket.hasCpuExact || bucket.hasCpuChild;

		if (auto it = summary.features.find(rootName); it != summary.features.end()) {
			auto& totals = it->second;
			if (hasGpu) {
				totals.gpuAvgMs = gpuMs;
				totals.hasGpu = true;
			}
			if (hasCpu) {
				totals.cpuAvgMs = cpuMs;
				totals.hasCpu = true;
			}
		}
	}

	summary.valid =
		summary.hasPresentInterval ||
		summary.hasWholeFrameGpu ||
		summary.hasWholeFrameCpu;
	return summary;
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
	if (RenderFeatureTimingGraph(featurePrefixes.front(), gpuData, graphState.gpuGraph, 82)) {
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
	if (RenderFeatureTimingGraph(featurePrefixes.front(), cpuData, graphState.cpuGraph, 82)) {
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
