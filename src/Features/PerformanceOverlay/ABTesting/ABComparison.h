#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

enum class ABVariant
{
	A,
	B
};

namespace ABComparison
{
	inline constexpr size_t MinimumSamples = 10;
	inline constexpr size_t MaximumSamples = 4096;
	inline constexpr size_t MaximumIntervals = 11;
	inline constexpr float NoiseFloorMs = 0.05f;

	/** @brief Completed source-frame range belonging to one settled configuration. */
	struct Interval
	{
		ABVariant variant = ABVariant::A;
		uint64_t first = 0, last = 0, lastObserved = 0;
		bool closed = false;
		std::array<std::vector<float>, 3> samples;
	};

	/** @brief B minus its two neighbouring A baselines; uncertainty is a conservative heuristic, not a confidence interval. */
	struct Metric
	{
		bool valid = false;
		size_t pairs = 0;
		double baseline = 0, change = 0, delta = 0, uncertainty = 0;
		int verdict = 0;
	};

	/** @brief Routes delayed samples by original frame identity; false reports exhausted bounded storage. */
	bool Add(std::vector<Interval>& intervals, uint64_t frame, const std::array<std::optional<float>, 3>& values);
	/** @brief Requires complete A-B-A ranges and at least 80 percent valid coverage on each side. */
	Metric Compare(const std::vector<Interval>& intervals, size_t metric);
}
