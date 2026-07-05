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
#include "RE/B/BSOpenVR.h"
#include "RE/M/Misc.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

static constexpr float kGraphHeadroomScale = 1.2f;
static constexpr float kMainGraphHeight = 180.0f;
static constexpr float kMainGraphMinFrameTimeSec = 0.0001f;
static constexpr float kFeatureGraphMinFrameTimeSec = 0.00001f;
static constexpr float kTimingTableMetricColumnWidth = 55.0f;
static constexpr float kTimingTablePercentColumnWidth = 45.0f;
static constexpr float kStatsRefreshSeconds = 1.0f;
static constexpr uint32_t kDisplayedRollingFrameCount = 60;

static bool IsPositiveFinite(float value)
{
	return std::isfinite(value) && value > 0.0f;
}

struct TimingAverage
{
	void Push(float value)
	{
		if (!IsPositiveFinite(value))
			return;

		sum += value;
		count++;
	}

	[[nodiscard]] bool HasSamples() const { return count > 0; }
	[[nodiscard]] float Get() const { return HasSamples() ? sum / static_cast<float>(count) : 0.0f; }

	float sum = 0.0f;
	uint32_t count = 0;
};

struct RollingTimingAverage
{
	void Push(float value)
	{
		if (!IsPositiveFinite(value))
			return;

		samples[head] = value;
		head = (head + 1) % kDisplayedRollingFrameCount;
		count = std::min<uint32_t>(count + 1, kDisplayedRollingFrameCount);
	}

	[[nodiscard]] float Get() const
	{
		TimingAverage average;
		for (uint32_t i = 0; i < count; ++i)
			average.Push(samples[i]);

		return average.Get();
	}

	std::array<float, kDisplayedRollingFrameCount> samples{};
	uint32_t head = 0;
	uint32_t count = 0;
};

static float GetAverageGameFrameMs()
{
	static RollingTimingAverage frameMsAverage;
	static uint32_t lastFrameCount = 0;
	static LARGE_INTEGER frequency{};
	static LARGE_INTEGER lastCounter{};

	const uint32_t frameCount = globals::state ? globals::state->frameCount : 0;
	if (frameCount != 0 && frameCount != lastFrameCount) {
		const bool consecutiveFrame = lastFrameCount != 0 && frameCount == lastFrameCount + 1;

		if (frequency.QuadPart == 0)
			QueryPerformanceFrequency(&frequency);

		LARGE_INTEGER currentCounter{};
		QueryPerformanceCounter(&currentCounter);

		float presentFrameMs = 0.0f;
		if (consecutiveFrame && frequency.QuadPart > 0 && lastCounter.QuadPart != 0) {
			presentFrameMs = static_cast<float>(
				static_cast<double>(currentCounter.QuadPart - lastCounter.QuadPart) * 1000.0 /
				static_cast<double>(frequency.QuadPart));
		}

		const float engineFrameMs = RE::GetSecondsSinceLastFrame() * 1000.0f;
		const bool hasEngineFrame = IsPositiveFinite(engineFrameMs);
		const bool hasPresentFrame = IsPositiveFinite(presentFrameMs);
		if (hasEngineFrame || hasPresentFrame) {
			frameMsAverage.Push(
				hasEngineFrame && hasPresentFrame ?
					std::max(engineFrameMs, presentFrameMs) :
					(hasEngineFrame ? engineFrameMs : presentFrameMs));
		}

		lastCounter = currentCounter;
		lastFrameCount = frameCount;
	}

	return frameMsAverage.Get();
}

static void CaptureOpenVRGameTiming(ProfilingRenderer::PerformanceTimingSummary& summary)
{
	if (!REL::Module::IsVR())
		return;

	auto* openvr = RE::BSOpenVR::GetSingleton();
	auto* compositor = openvr ? RE::BSOpenVR::GetIVRCompositor() : nullptr;
	if (!compositor && openvr)
		compositor = openvr->vrContext.vrCompositor;
	if (!compositor)
		return;

	std::array<vr::Compositor_FrameTiming, kDisplayedRollingFrameCount> timings{};
	for (auto& timing : timings)
		timing.m_nSize = static_cast<uint32_t>(sizeof(timing));

	uint32_t frameCount = std::min<uint32_t>(
		compositor->GetFrameTimings(timings.data(), static_cast<uint32_t>(timings.size())),
		static_cast<uint32_t>(timings.size()));
	if (frameCount == 0) {
		if (!compositor->GetFrameTiming(timings.data(), 0))
			return;
		frameCount = 1;
	}

	TimingAverage gpuAverage;
	TimingAverage cpuAverage;
	for (uint32_t i = 0; i < frameCount; ++i) {
		const auto& timing = timings[i];
		gpuAverage.Push(timing.m_flPreSubmitGpuMs);
		cpuAverage.Push(timing.m_flNewFrameReadyMs - timing.m_flWaitGetPosesCalledMs);
	}

	if (gpuAverage.HasSamples()) {
		summary.gameGpuMs = gpuAverage.Get();
		summary.hasGameGpu = true;
	}

	if (cpuAverage.HasSamples()) {
		summary.gameCpuMs = cpuAverage.Get();
		summary.hasGameCpu = true;
	}
}

static void NormalizeGameFrameTiming(ProfilingRenderer::PerformanceTimingSummary& summary)
{
	float frameMs = summary.frameMs;
	if (summary.hasGameGpu)
		frameMs = std::max(frameMs, summary.gameGpuMs);
	if (summary.hasGameCpu)
		frameMs = std::max(frameMs, summary.gameCpuMs);

	if (IsPositiveFinite(frameMs)) {
		summary.frameMs = frameMs;
		summary.fps = 1000.0f / frameMs;
	}
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
	TextWithTuningDelta(direction, "Avg %.3f ms", avgMs);
	TextWithTuningDelta(direction, "P95 %.3f", p95Ms);
	TextWithTuningDelta(direction, "P99 %.3f", p99Ms);
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

struct DisplayTimingStats
{
	float timeMs = 0.0f;
	float avgMs = 0.0f;
	float p95Ms = 0.0f;
	float p99Ms = 0.0f;
};

static uint32_t CollectDisplayTimingSamples(const Profiler::TimerResult& result, bool cpuMode, std::array<float, kDisplayedRollingFrameCount>& samples)
{
	samples.fill(0.0f);

	const uint32_t historyCount = cpuMode ? result.cpuHistoryCount : result.historyCount;
	const uint32_t sampleCount = std::min(historyCount, kDisplayedRollingFrameCount);
	if (sampleCount == 0)
		return 0;

	const uint32_t firstSample = historyCount - sampleCount;
	for (uint32_t i = 0; i < sampleCount; ++i) {
		samples[i] = cpuMode ?
		                 result.GetCpuHistorySample(firstSample + i) :
		                 result.GetHistorySample(firstSample + i);
	}

	return sampleCount;
}

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

	ImGui::PushID("ProfilingTimingMode");
	ImGui::RadioButton("GPU", &mode, static_cast<int>(TimingMode::GPU));
	ImGui::SameLine();
	ImGui::RadioButton("CPU", &mode, static_cast<int>(TimingMode::CPU));
	ImGui::PopID();

	const auto newMode = static_cast<TimingMode>(mode);
	if (newMode != timingMode) {
		timingMode = newMode;
		timeSinceLastUpdate = kStatsRefreshSeconds;
	}
}

void ProfilingRenderer::SetupTimingTableColumns(float passColumnWidth, bool includePercentColumn)
{
	const float scale = Util::GetUIScale();
	ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, passColumnWidth);
	ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, kTimingTableMetricColumnWidth * scale);
	ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed, kTimingTableMetricColumnWidth * scale);
	ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthFixed, kTimingTableMetricColumnWidth * scale);
	if (includePercentColumn)
		ImGui::TableSetupColumn("%%", ImGuiTableColumnFlags_WidthFixed, kTimingTablePercentColumnWidth * scale);
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

bool ProfilingRenderer::RenderFeatureTimingGraph(const std::string& featurePrefix, const FeatureTimingData& data, ImGuiUtils::ProfilerGraph& graph, int graphHeight)
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

	graph.LoadFrameData(tasks.data(), tasks.size());

	float maxFrameTimeSec = graph.GetPeakFrameTime() * kGraphHeadroomScale;
	if (maxFrameTimeSec < kFeatureGraphMinFrameTimeSec)
		maxFrameTimeSec = kFeatureGraphMinFrameTimeSec;

	const float uiScale = Util::GetUIScale();
	const int totalWidth = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x));
	const int legendWidth = totalWidth >= ScaleToUiInt(180.0f) ? ComputeGraphLegendWidth(totalWidth, ScaleToUiInt(80.0f), 0.50f, ScaleToUiInt(140.0f), ScaleToUiInt(300.0f)) : 0;
	const int graphWidth = std::max(1, totalWidth - legendWidth);
	const float scaledGraphHeight = static_cast<float>(graphHeight) * uiScale;

	graph.RenderTimings(static_cast<float>(graphWidth), static_cast<float>(legendWidth), scaledGraphHeight, 0, maxFrameTimeSec, uiScale);
	return true;
}

bool ProfilingRenderer::RenderFeatureTimingData(const std::string& featurePrefix, FeatureTimingMode featureMode, bool showTable)
{
	bool cpuMode = featureMode == FeatureTimingMode::CPU;
	const auto data = CollectFeatureTimingData(featurePrefix, cpuMode);

	if (data.entries.empty()) {
		ImGui::TextDisabled("No timing data");
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
		passLabels.emplace_back("Total");
		SetupTimingTableColumns(GetTextColumnWidth("Pass", passLabels, GetColorMarkerExtraWidth()), false);
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
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "Total");
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

	ImGui::SeparatorText("Feature Profiling Overview");

	if (ImGui::BeginTable("##FeatureProfilingOverview", 3,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthFixed, GetTextColumnWidth("Feature", activeFeatures));
		ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
				ImGui::TextDisabled("No GPU timing data");
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID((featurePrefix + "::CPU").c_str());
			if (!RenderFeatureTimingGraph(featurePrefix, cpuData, state.cpuGraph, 85))
				ImGui::TextDisabled("No CPU timing data");
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
		ImGui::TextUnformatted("Profiling");
		ImGui::SameLine();
		if (ImGui::Checkbox("Enable", &profilingEnabled)) {
			profiler.SetUserEnabled(profilingEnabled);
			timeSinceLastUpdate = kStatsRefreshSeconds;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Runtime profiling capture. No restart required.");
			ImGui::TextUnformatted("When off, profiling capture requests are ignored and no timestamp/query timing scopes are recorded.");
		}
		ImGui::Separator();

		if (!profilingEnabled) {
			ImGui::TextDisabled("Profiling is off.");
			return;
		}
	} else if (!profiler.IsUserEnabled()) {
		profiler.SetUserEnabled(true);
	}

	profiler.RequestCapture();

	bool cpuMode = (timingMode == TimingMode::CPU);
	if (showModeToggle) {
		RenderTimingModeToggle();
		cpuMode = (timingMode == TimingMode::CPU);
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
			ImGui::TextDisabled("No timing data available (enter game world)");
		return;
	}

	if (renderedFeatureOverview)
		ImGui::SeparatorText("All Timings");
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
	auto& profiler = (*globals::profiler);
	auto& featureMode = featureTimingModes[featurePrefix];

	int mode = static_cast<int>(featureMode);
	const int previousMode = mode;
	ImGui::RadioButton("Off", &mode, static_cast<int>(FeatureTimingMode::Off));
	ImGui::SameLine();
	ImGui::RadioButton("GPU", &mode, static_cast<int>(FeatureTimingMode::GPU));
	ImGui::SameLine();
	ImGui::RadioButton("CPU", &mode, static_cast<int>(FeatureTimingMode::CPU));

	mode = std::clamp(mode, static_cast<int>(FeatureTimingMode::Off), static_cast<int>(FeatureTimingMode::CPU));
	if (mode != previousMode) {
		featureMode = static_cast<FeatureTimingMode>(mode);
		if (featureMode != FeatureTimingMode::Off)
			profiler.SetUserEnabled(true);
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Off: do not request profiling capture for this feature.");
		ImGui::TextUnformatted("GPU/CPU: enable runtime profiling and show this feature's timing in the selected mode.");
		ImGui::TextUnformatted("No restart required.");
	}

	if (featureMode == FeatureTimingMode::Off) {
		ImGui::TextDisabled("Feature profiling is off.");
		return;
	}

	if (!profiler.IsUserEnabled()) {
		ImGui::TextDisabled("Runtime profiling is off.");
		return;
	}

	profiler.RequestCapture();
	RenderFeatureTimingData(featurePrefix, featureMode, true);
}

ProfilingRenderer::PerformanceTimingSummary ProfilingRenderer::CapturePerformanceTimingSummary(const std::vector<std::string>& featurePrefixes, bool requestCapture)
{
	PerformanceTimingSummary summary;
	if (!globals::profiler) {
		return summary;
	}

	auto& profiler = (*globals::profiler);
	if (requestCapture && !profiler.IsUserEnabled())
		profiler.SetUserEnabled(true);
	if (requestCapture)
		profiler.RequestCapture();

	struct FeaturePrefixLookup
	{
		std::string featurePrefix;
	};
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

	std::vector<FeaturePrefixLookup> prefixLookup;
	prefixLookup.reserve(featurePrefixes.size());
	for (const auto& featurePrefix : featurePrefixes) {
		summary.features.try_emplace(featurePrefix);
		prefixLookup.push_back({ featurePrefix });
	}
	auto isRequestedFeatureRoot = [&](const std::string& rootName) {
		if (prefixLookup.empty())
			return true;

		for (const auto& lookup : prefixLookup) {
			if (lookup.featurePrefix == rootName)
				return true;
		}

		return false;
	};

	std::unordered_map<std::string, TimingBucket> timingBuckets;
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

	summary.frameMs = GetAverageGameFrameMs();
	summary.fps = summary.frameMs > 0.0f ? 1000.0f / summary.frameMs : 0.0f;
	CaptureOpenVRGameTiming(summary);
	NormalizeGameFrameTiming(summary);
	for (const auto& [rootName, bucket] : timingBuckets) {
		const float gpuMs = bucket.hasGpuExact ? bucket.gpuExactMs : bucket.gpuChildMs;
		const float cpuMs = bucket.hasCpuExact ? bucket.cpuExactMs : bucket.cpuChildMs;
		const bool hasGpu = bucket.hasGpuExact || bucket.hasGpuChild;
		const bool hasCpu = bucket.hasCpuExact || bucket.hasCpuChild;

		if (hasGpu)
			summary.gpuTotalMs += gpuMs;
		if (hasCpu)
			summary.cpuTotalMs += cpuMs;

		for (const auto& lookup : prefixLookup) {
			if (rootName != lookup.featurePrefix)
				continue;

			auto& totals = summary.features[lookup.featurePrefix];
			if (hasGpu) {
				totals.gpuAvgMs += gpuMs;
				totals.hasGpu = true;
			}
			if (hasCpu) {
				totals.cpuAvgMs += cpuMs;
				totals.hasCpu = true;
			}
			break;
		}
	}
	summary.valid =
		summary.frameMs > 0.0f ||
		summary.hasGameGpu ||
		summary.hasGameCpu ||
		summary.gpuTotalMs > 0.0f ||
		summary.cpuTotalMs > 0.0f;
	return summary;
}

void ProfilingRenderer::RenderFeaturePerformanceSummary(
	const std::string& featurePrefix,
	const PerformanceTimingHighlight* highlight,
	const PerformanceTimingSummary* summaryOverride)
{
	RenderFeaturePerformanceSummary(std::vector<std::string>{ featurePrefix }, highlight, summaryOverride);
}

void ProfilingRenderer::RenderFeaturePerformanceSummary(
	const std::vector<std::string>& featurePrefixes,
	const PerformanceTimingHighlight* highlight,
	const PerformanceTimingSummary* /*summaryOverride*/)
{
	if (!globals::profiler) {
		ImGui::TextDisabled("No profiler available.");
		return;
	}

	if (featurePrefixes.empty()) {
		ImGui::TextDisabled("No feature selected.");
		return;
	}

	const auto gpuData = CollectFeatureTimingData(featurePrefixes, false);
	const auto cpuData = CollectFeatureTimingData(featurePrefixes, true);
	auto& graphState = featureGraphs[BuildTimingPrefixKey(featurePrefixes)];

	if (!gpuData.entries.empty() || !cpuData.entries.empty()) {
		if (!gpuData.entries.empty())
			TextWithTuningDelta(highlight ? highlight->featureGpuDirection : 0, "Feat GPU %.3f ms", gpuData.totalAvg);
		if (!gpuData.entries.empty() && !cpuData.entries.empty())
			ImGui::SameLine();
		if (!cpuData.entries.empty())
			TextWithTuningDelta(highlight ? highlight->featureCpuDirection : 0, "CPU %.3f ms", cpuData.totalAvg);
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("GPU");
	ImGui::PushID("PerformanceSummaryGPU");
	if (RenderFeatureTimingGraph(featurePrefixes.front(), gpuData, graphState.gpuGraph, 82)) {
		RenderFeatureTimingStats(gpuData.totalAvg, gpuData.totalP95, gpuData.totalP99, highlight ? highlight->featureGpuDirection : 0);
	} else {
		ImGui::TextDisabled("No GPU timing data");
	}
	ImGui::PopID();

	ImGui::Spacing();
	ImGui::TextUnformatted("CPU");
	ImGui::PushID("PerformanceSummaryCPU");
	if (RenderFeatureTimingGraph(featurePrefixes.front(), cpuData, graphState.cpuGraph, 82)) {
		RenderFeatureTimingStats(cpuData.totalAvg, cpuData.totalP95, cpuData.totalP99, highlight ? highlight->featureCpuDirection : 0);
	} else {
		ImGui::TextDisabled("No CPU timing data");
	}
	ImGui::PopID();
}
