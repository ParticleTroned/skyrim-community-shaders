#pragma once

#include "Utils/DepthPyramidPolicy.h"
#include <algorithm>
#include <array>

namespace GrassHiZPolicy
{
	inline constexpr uint32_t kBaseReduction = 4;
	inline constexpr uint32_t kMaximumMips = 7;
	inline constexpr uint32_t kReuseProofs = 1, kCostAware = 2, kEarlyVisibility = 4;

	constexpr uint32_t BaseReduction(bool finerDepth) { return finerDepth ? 2u : kBaseReduction; }
	constexpr uint32_t MaximumMips(bool finerDepth) { return kMaximumMips + uint32_t(finerDepth); }

	/** @brief Optional cheap-LOD estimate can skip occlusion work; it never removes grass. */
	constexpr uint32_t CandidateMeshIndices(const std::array<uint32_t, 4>& indices, bool midEnabled, bool farEnabled, bool costAware)
	{
		if (!costAware)
			return *std::max_element(indices.begin(), indices.end());
		uint32_t result = indices[0];
		if (midEnabled && indices[1])
			result = std::min(result, indices[1]);
		if (farEnabled && indices[2])
			result = std::min(result, indices[2]);
		return result;
	}

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
	constexpr bool ShouldBuild(uint64_t candidateIndexWork, DepthPyramidPolicy::Extent extent, uint32_t eyes, uint32_t maximumMips = kMaximumMips)
	{
		return candidateIndexWork > DepthPyramidPolicy::OutputTexelWork(extent, eyes, maximumMips);
	}
}
