#include "ABTestAggregator.h"
#include "Utils/ProfilerTiming.h"
#include "Utils/Statistics.h"
#include <algorithm>
#include <cmath>
#include <map>

void ABTestAggregator::FinishInterval(Clock::time_point now)
{
	if (currentInterval) {
		if (!currentInterval->warmup) {
			currentInterval->endTime = now;
			intervals.push_back(std::move(*currentInterval));
		}
		currentInterval.reset();
	}
}

void ABTestAggregator::OnABSwitch(ABVariant variant, Clock::time_point now)
{
	if (currentInterval && currentInterval->variant == variant)
		return;
	FinishInterval(now);

	const bool warmup = initialBWarmupPending && variant == ABVariant::B;
	if (variant == ABVariant::A)
		initialBWarmupPending = false;

	currentInterval = std::make_unique<ABInterval>(variant, std::vector<std::vector<DrawCallRow>>{}, now, now, warmup);

	// Initialization costs must not contribute to measured duration.
	if (!warmup && intervals.empty()) {
		testStartTime = now;
	}
}

void ABTestAggregator::OnFrame(const std::vector<DrawCallRow>& rows)
{
	if (!currentInterval || currentInterval->warmup)
		return;

	const auto total = std::ranges::find_if(rows, [](const auto& row) { return row.shaderType == -1; });
	if (total == rows.end() || total->frameTime <= 0.0f || total->frameTime > kMaxOutlierFrameTime ||
		!std::ranges::all_of(rows, [](const auto& row) {
			// Other is a residual of independently sampled timings and can be negative.
			const float time = row.shaderType == -2 ? std::abs(row.frameTime) : row.frameTime;
			return Util::ProfilerTiming::IsValidSample(time);
		})) {
		++currentInterval->excludedFrames;
		return;
	}

	// A slower configuration needs its own baseline, not the faster variant's.
	auto& history = recentFrameTimes[currentInterval->variant == ABVariant::A ? 0 : 1];
	history.push_back(total->frameTime);
	if (history.size() > kFrameHistoryBaseline)
		history.erase(history.begin());
	if (history.size() >= kMinimumFramesForAnalysis && total->frameTime > Util::Median(history) * kOutlierMultiplier) {
		++currentInterval->excludedFrames;
		return;
	}
	currentInterval->frameRows.push_back(rows);
}

void ABTestAggregator::OnTestEnd(Clock::time_point now)
{
	if (!currentInterval)
		return;
	FinishInterval(now);
	if (!intervals.empty())
		testEndTime = now;
}

void ABTestAggregator::Clear()
{
	intervals.clear();
	currentInterval.reset();
	for (auto& history : recentFrameTimes)
		history.clear();
	testStartTime = {};
	testEndTime = {};
	initialBWarmupPending = true;
}

std::vector<AggregatedDrawCallStats> ABTestAggregator::GetAggregatedResults() const
{
	// Map: shaderType -> label
	std::map<int, std::string> labelMap;
	// Map shader types to their measured times in each configuration.
	std::map<int, std::vector<float>> aFrameTimes, bFrameTimes;

	for (const auto& interval : intervals) {
		for (const auto& frameRows : interval.frameRows) {
			for (const auto& row : frameRows) {
				labelMap[row.shaderType] = row.label;
				if (interval.variant == ABVariant::A) {
					aFrameTimes[row.shaderType].push_back(row.frameTime);
				} else {
					bFrameTimes[row.shaderType].push_back(row.frameTime);
				}
			}
		}
	}

	std::vector<AggregatedDrawCallStats> result;
	for (const auto& [shaderType, label] : labelMap) {
		AggregatedDrawCallStats stats;
		stats.label = label;
		stats.shaderType = shaderType;
		stats.meanA = Util::Mean(aFrameTimes[shaderType]);
		stats.meanB = Util::Mean(bFrameTimes[shaderType]);
		stats.medianA = Util::Median(aFrameTimes[shaderType]);
		stats.medianB = Util::Median(bFrameTimes[shaderType]);
		stats.delta = stats.meanB - stats.meanA;
		stats.frameCountA = static_cast<int>(aFrameTimes[shaderType].size());
		stats.frameCountB = static_cast<int>(bFrameTimes[shaderType].size());
		stats.totalTimeA = std::accumulate(aFrameTimes[shaderType].begin(), aFrameTimes[shaderType].end(), 0.0f);
		stats.totalTimeB = std::accumulate(bFrameTimes[shaderType].begin(), bFrameTimes[shaderType].end(), 0.0f);
		result.push_back(stats);
	}

	// Sort: put Total (-1) and Other (-2) at the end
	std::sort(result.begin(), result.end(), [](const AggregatedDrawCallStats& a, const AggregatedDrawCallStats& b) {
		// Special handling for summary rows - always put them at the end
		if (a.shaderType == -1 || a.shaderType == -2) {
			if (b.shaderType == -1 || b.shaderType == -2) {
				// Both are summary rows, sort by shader type (Other before Total)
				return a.shaderType < b.shaderType;
			}
			return false;  // a is summary, b is not, so a goes after b
		}
		if (b.shaderType == -1 || b.shaderType == -2) {
			return true;  // b is summary, a is not, so b goes after a
		}
		// Both are regular shaders, sort normally
		return a.shaderType < b.shaderType;
	});

	return result;
}

ABVariantStatistics ABTestAggregator::GetVariantStatistics(ABVariant variant) const
{
	ABVariantStatistics result;
	for (const auto& interval : intervals) {
		if (interval.variant == variant) {
			result.frames += static_cast<int>(interval.frameRows.size());
			result.excludedFrames += interval.excludedFrames;
			result.duration += std::chrono::duration<float>(interval.endTime - interval.startTime).count();
		}
	}
	return result;
}

ABTestCoverage ABTestAggregator::GetCoverage() const
{
	const std::array stats{ GetVariantStatistics(ABVariant::A), GetVariantStatistics(ABVariant::B) };
	if (std::ranges::all_of(stats, [](const auto& value) {
			return value.frames >= kMinimumSamplesForValidity && value.duration >= kMinimumTestDuration &&
		           value.ValidPercent() >= kMinimumValidFramesPercent;
		}))
		return ABTestCoverage::Sufficient;
	if (std::ranges::all_of(stats, [](const auto& value) {
			return value.frames >= kMinimumSamplesForMarginal && value.duration >= kMinimumDurationForMarginal &&
		           value.ValidPercent() >= kMinimumValidFramesPercent;
		}))
		return ABTestCoverage::Marginal;
	return ABTestCoverage::Insufficient;
}

float ABTestAggregator::GetTotalTestDuration() const
{
	return GetVariantStatistics(ABVariant::A).duration + GetVariantStatistics(ABVariant::B).duration;
}

int ABTestAggregator::GetTotalFrameCount() const
{
	int total = 0;
	for (const auto& interval : intervals) {
		total += static_cast<int>(interval.frameRows.size());
	}
	return total;
}
