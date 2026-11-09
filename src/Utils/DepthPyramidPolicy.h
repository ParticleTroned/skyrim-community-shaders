#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>

namespace DepthPyramidPolicy
{
	struct Extent
	{
		uint32_t width = 0, height = 0;
		bool operator==(const Extent&) const = default;
	};

	/** @brief Fit whole active eyes to independent power-of-two reduction chains. */
	constexpr std::optional<Extent> ActiveExtent(uint32_t activeWidth, uint32_t activeHeight, uint32_t eyes, uint32_t reduction)
	{
		if ((eyes != 1 && eyes != 2) || (reduction != 2 && reduction != 4) ||
			!activeWidth || !activeHeight || activeWidth % eyes)
			return std::nullopt;
		const auto width = std::bit_ceil((uint64_t(activeWidth / eyes) + reduction - 1) / reduction) * eyes;
		const auto height = std::bit_ceil((uint64_t(activeHeight) + reduction - 1) / reduction);
		if (width > UINT32_MAX || height > UINT32_MAX)
			return std::nullopt;
		return Extent{ uint32_t(width), uint32_t(height) };
	}

	/** @brief Grow immediately, but require consecutive low-demand frames before shrinking. */
	class AllocationRetention
	{
	public:
		Extent Observe(Extent current, Extent required, uint32_t frame, uint32_t shrinkFrames)
		{
			if (!current.width || !current.height || required.width > current.width || required.height > current.height) {
				*this = {};
				return { std::max(current.width, required.width), std::max(current.height, required.height) };
			}
			if (current == required) {
				*this = {};
				return current;
			}
			if (lastFrame == frame)
				return current;
			const bool consecutive = lastFrame <= UINT32_MAX && frame - uint32_t(lastFrame) == 1;
			if (!consecutive || pending != required)
				lowDemandFrames = 0;
			pending = required;
			lastFrame = frame;
			lowDemandFrames += lowDemandFrames < shrinkFrames;
			if (lowDemandFrames < shrinkFrames)
				return current;
			*this = {};
			return required;
		}

	private:
		Extent pending{};
		uint64_t lastFrame = UINT64_MAX;
		uint32_t lowDemandFrames = 0;
	};

	/** @brief Stop the chain before reductions could combine the two eye regions. */
	constexpr uint32_t MipCount(Extent extent, uint32_t eyes, uint32_t maximumMips)
	{
		if ((eyes != 1 && eyes != 2) || extent.width % eyes ||
			!std::has_single_bit(extent.width / eyes) || !std::has_single_bit(extent.height))
			return 0;
		const auto complete = uint32_t(std::min(std::bit_width(extent.width / eyes), std::bit_width(extent.height)));
		return maximumMips ? std::min(complete, maximumMips) : complete;
	}

	/** @brief Count produced texels, saturating instead of underestimating oversized inputs. */
	constexpr uint64_t OutputTexelWork(Extent extent, uint32_t eyes, uint32_t maximumMips)
	{
		const auto mips = MipCount(extent, eyes, maximumMips);
		if (!mips)
			return UINT64_MAX;
		uint64_t total = 0;
		for (uint32_t mip = 0; mip < mips; ++mip) {
			const auto pixels = uint64_t(extent.width >> mip) * (extent.height >> mip);
			if (pixels > UINT64_MAX - total)
				return UINT64_MAX;
			total += pixels;
		}
		return total;
	}

	/** @brief Limit SPD to local tiles when its single tail group cannot cover the full source. */
	constexpr uint32_t SinglePassMipCount(uint32_t width, uint32_t height, uint32_t totalMips)
	{
		constexpr uint32_t maxTailSourceExtent = 4096;
		constexpr uint32_t localMipCount = 6, completeMipCount = 12;
		const auto limit = width > maxTailSourceExtent || height > maxTailSourceExtent ? localMipCount : completeMipCount;
		return totalMips > 1 ? (std::min)(totalMips - 1, limit) : 0;
	}
}
