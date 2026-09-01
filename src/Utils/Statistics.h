#pragma once

#include <algorithm>
#include <numeric>
#include <vector>

namespace Util
{
	/** Mean of the samples, or zero for an empty set. */
	inline float Mean(const std::vector<float>& values)
	{
		if (values.empty())
			return 0.0f;
		return std::accumulate(values.begin(), values.end(), 0.0f) / values.size();
	}

	/** Median of the samples, averaging the two middle values for even counts. */
	inline float Median(std::vector<float> values)
	{
		if (values.empty())
			return 0.0f;
		const auto middle = values.begin() + values.size() / 2;
		std::nth_element(values.begin(), middle, values.end());
		return values.size() % 2 ? *middle :
		                           std::midpoint(*std::max_element(values.begin(), middle), *middle);
	}
}
