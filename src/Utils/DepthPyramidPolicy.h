#pragma once

#include <algorithm>
#include <cstdint>

namespace DepthPyramidPolicy
{
	/** @brief Limit SPD to local tiles when its single tail group cannot cover the full source. */
	constexpr uint32_t SinglePassMipCount(uint32_t width, uint32_t height, uint32_t totalMips)
	{
		constexpr uint32_t maxTailSourceExtent = 4096;
		constexpr uint32_t localMipCount = 6, completeMipCount = 12;
		const auto limit = width > maxTailSourceExtent || height > maxTailSourceExtent ? localMipCount : completeMipCount;
		return totalMips > 1 ? (std::min)(totalMips - 1, limit) : 0;
	}
}
