#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Util::ProfilerTiming
{
	inline constexpr double kMaxSampleMs = 1000.0;

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
