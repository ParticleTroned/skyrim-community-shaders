#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Util::ProfilerTiming
{
	inline constexpr double kMaxSampleMs = 1000.0;

	struct Statistics
	{
		float average = 0.0f;
		float p95 = 0.0f;
		float p99 = 0.0f;
	};

	/** Preserve sample order for the mean and share one sort for both percentiles. */
	inline Statistics CalculateStatistics(std::span<const float> samples, bool includePercentiles = true)
	{
		Statistics result;
		if (samples.empty())
			return result;
		float sum = 0.0f;
		for (const float sample : samples)
			sum += sample;
		result.average = sum / static_cast<float>(samples.size());
		if (includePercentiles) {
			thread_local std::vector<float> sorted;
			sorted.assign(samples.begin(), samples.end());
			std::sort(sorted.begin(), sorted.end());
			const auto percentile = [&](float p) {
				const float index = (p / 100.0f) * static_cast<float>(sorted.size() - 1);
				const auto lo = static_cast<size_t>(index);
				const auto hi = std::min(lo + 1, sorted.size() - 1);
				const float fraction = index - static_cast<float>(lo);
				return sorted[lo] * (1.0f - fraction) + sorted[hi] * fraction;
			};
			result.p95 = percentile(95.0f);
			result.p99 = percentile(99.0f);
		}
		return result;
	}

	/** Reject unavailable queries and implausible samples independently in each domain. */
	inline bool IsValidSample(double ms)
	{
		return std::isfinite(ms) && ms >= 0.0 && ms <= kMaxSampleMs;
	}

	struct Interval
	{
		int32_t parent = -1;
		double inclusiveMs = -1.0;
	};

	/** Resolve intervals in acquisition order, promoting children past invalid parents. */
	inline std::vector<double> ResolveSelfTimes(std::span<const Interval> intervals)
	{
		std::vector<double> nested(intervals.size(), 0.0);
		std::vector<double> self(intervals.size(), 0.0);
		for (size_t i = 0; i < intervals.size(); ++i) {
			if (!IsValidSample(intervals[i].inclusiveMs))
				continue;
			auto child = i;
			auto parent = intervals[i].parent;
			while (parent >= 0 && static_cast<size_t>(parent) < child) {
				if (IsValidSample(intervals[parent].inclusiveMs)) {
					nested[parent] += intervals[i].inclusiveMs;
					break;
				}
				child = static_cast<size_t>(parent);
				parent = intervals[parent].parent;
			}
		}
		for (size_t i = 0; i < intervals.size(); ++i) {
			if (IsValidSample(intervals[i].inclusiveMs))
				self[i] = std::max(intervals[i].inclusiveMs - nested[i], 0.0);
		}
		return self;
	}

	struct CpuCompletion
	{
		double selfMs = 0.0;
		double coveredMs = 0.0;
	};

	/** Pass valid descendant coverage through an invalid CPU scope without counting it twice. */
	inline CpuCompletion CompleteCpuScope(double inclusiveMs, double nestedMs)
	{
		return IsValidSample(inclusiveMs) ?
		           CpuCompletion{ std::max(inclusiveMs - nestedMs, 0.0), inclusiveMs } :
		           CpuCompletion{ 0.0, nestedMs };
	}
}
