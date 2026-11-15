#pragma once

#include "Utils/DepthPyramidPolicy.h"

namespace GrassHiZPolicy
{
	inline constexpr uint32_t kBaseReduction = 4;
	inline constexpr uint32_t kMaximumMips = 7;

	/** @brief Bound potential indexed work without wrapping large candidate totals. */
	constexpr uint64_t AddCandidateIndexWork(uint64_t current, uint32_t instances, uint32_t eyes, uint32_t indices)
	{
		if (eyes != 1 && eyes != 2)
			return UINT64_MAX;
		const auto singleEye = uint64_t(instances) * indices;
		if (singleEye > (UINT64_MAX - current) / eyes)
			return UINT64_MAX;
		return current + singleEye * eyes;
	}

	/** @brief Screen sparse work by indexed requests versus hierarchy texels; cache and pixel costs remain unmodeled. */
	constexpr bool ShouldBuild(uint64_t candidateIndexWork, DepthPyramidPolicy::Extent extent, uint32_t eyes)
	{
		return candidateIndexWork > DepthPyramidPolicy::OutputTexelWork(extent, eyes, kMaximumMips);
	}
}
