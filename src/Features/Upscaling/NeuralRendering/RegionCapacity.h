#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kEyeCount = 2;
	inline constexpr std::uint32_t kLogicalFeatureSlotCount = 4;
	inline constexpr std::uint32_t kPhysicalFeatureSlotCount = kLogicalFeatureSlotCount;
	inline constexpr std::size_t kMaximumRegionEvaluations = kEyeCount;
	static_assert(kPhysicalFeatureSlotCount <= std::numeric_limits<std::uint32_t>::digits);

	/** Experimental native contexts below this extent have no completion qualification. */
	inline constexpr std::uint32_t kMinimumExperimentalContextExtent = 128;

	/** Applies the same completion qualification floor to either rectangle axis. */
	template <class Rectangle>
	[[nodiscard]] constexpr bool QualifiedExperimentalContextGeometry(const Rectangle& rectangle) noexcept
	{
		return rectangle.width >= kMinimumExperimentalContextExtent &&
		       rectangle.height >= kMinimumExperimentalContextExtent;
	}

	/** Invalid indices cannot wrap a shift into another physical slot. */
	[[nodiscard]] constexpr std::uint32_t FeatureSlotBit(std::uint32_t slot) noexcept
	{
		return slot < kPhysicalFeatureSlotCount ? std::uint32_t{ 1 } << slot : 0u;
	}

	/** Each route owns one feature per eye. */
	[[nodiscard]] constexpr std::uint32_t RegionRouteMask(std::uint32_t logicalMask) noexcept
	{
		return logicalMask & 0xFu;
	}

	[[nodiscard]] constexpr std::uint32_t LogicalRegionMask(std::uint32_t physicalMask) noexcept
	{
		return physicalMask & 0xFu;
	}
}
