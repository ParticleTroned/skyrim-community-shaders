#include "ABComparison.h"

#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(AB_COMPARISON_TEST)

#	include <algorithm>
#	include <cmath>
#	include <numeric>

namespace ABComparison
{
	bool Add(std::vector<Interval>& intervals, uint64_t frame, const std::array<std::optional<float>, 3>& values)
	{
		for (auto& interval : intervals) {
			if (frame < interval.first || (interval.closed && frame > interval.last) || frame <= interval.lastObserved)
				continue;
			interval.lastObserved = frame;
			for (size_t i = 0; i < values.size(); ++i) {
				if (!values[i] || !std::isfinite(*values[i]) || *values[i] < 0 || *values[i] > 1000 || (i == 0 && *values[i] == 0))
					continue;
				if (interval.samples[i].size() >= MaximumSamples)
					return false;
				interval.samples[i].push_back(*values[i]);
			}
			return true;
		}
		return true;
	}

	Metric Compare(const std::vector<Interval>& intervals, size_t metric)
	{
		Metric result;
		if (metric >= 3 || intervals.size() < 3 || intervals.size() > MaximumIntervals || intervals.size() % 2 == 0)
			return result;
		for (size_t i = 0; i < intervals.size(); ++i) {
			if (intervals[i].variant != (i % 2 == 0 ? ABVariant::A : ABVariant::B) ||
				(i > 0 && intervals[i].first <= intervals[i - 1].last))
				return result;
		}
		const auto usable = [metric](const Interval& interval) {
			const auto count = interval.samples[metric].size();
			return interval.closed && interval.last >= interval.first && count >= MinimumSamples && count <= MaximumSamples &&
			       double(count) <= double(interval.last - interval.first) + 1.0 &&
			       double(count) >= 0.8 * (double(interval.last - interval.first) + 1.0);
		};
		const auto mean = [metric](const Interval& interval) {
			const auto& values = interval.samples[metric];
			return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
		};
		const auto variance = [metric](const Interval& interval, double average) {
			double sum = 0;
			for (float value : interval.samples[metric])
				sum += (value - average) * (value - average);
			const double count = static_cast<double>(interval.samples[metric].size());
			return sum / (count * (count - 1));
		};
		double lowest = 0, highest = 0;
		for (size_t i = 0; i < intervals.size(); ++i) {
			if (intervals[i].variant != ABVariant::B)
				continue;
			// A missing bracket invalidates the comparison instead of selecting only favourable intervals.
			if (i == 0 || i + 1 == intervals.size() || intervals[i - 1].variant != ABVariant::A ||
				intervals[i + 1].variant != ABVariant::A || !usable(intervals[i - 1]) || !usable(intervals[i]) || !usable(intervals[i + 1]))
				return {};
			const double before = mean(intervals[i - 1]), after = mean(intervals[i + 1]), change = mean(intervals[i]);
			const double baseline = (before + after) * 0.5;
			const double delta = change - baseline;
			const double error = 2.0 * std::sqrt(variance(intervals[i], change) +
												 0.25 * (variance(intervals[i - 1], before) + variance(intervals[i + 1], after)));
			result.uncertainty = std::max({ result.uncertainty, std::abs(before - after) * 0.5, error });
			lowest = result.pairs ? std::min(lowest, delta) : delta;
			highest = result.pairs ? std::max(highest, delta) : delta;
			result.baseline += baseline;
			result.change += change;
			++result.pairs;
		}
		if (!result.pairs)
			return result;
		result.baseline /= result.pairs;
		result.change /= result.pairs;
		result.delta = result.change - result.baseline;
		result.uncertainty = std::max(result.uncertainty, (highest - lowest) * 0.5);
		result.valid = std::isfinite(result.delta) && std::isfinite(result.uncertainty);
		if (result.valid && std::abs(result.delta) > std::max(result.uncertainty, double(NoiseFloorMs)))
			result.verdict = result.delta < 0 ? -1 : 1;
		return result;
	}
}

#endif
