#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kEyeCount = 2;
	inline constexpr std::uint32_t kLogicalFeatureSlotCount = 4;
	inline constexpr std::uint32_t kDefaultRegionsPerEye = 2;
	inline constexpr std::uint32_t kTargetRegionsPerEye = 4;
	inline constexpr std::uint32_t kMaximumRegionsPerEye = 8;
#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr std::uint32_t kEnabledRegionsPerEye = kMaximumRegionsPerEye;
#else
	inline constexpr std::uint32_t kEnabledRegionsPerEye = kDefaultRegionsPerEye;
#endif
	inline constexpr std::uint32_t kPhysicalFeatureSlotCount = kLogicalFeatureSlotCount * kEnabledRegionsPerEye;
	inline constexpr std::size_t kMaximumRegionEvaluations = kEyeCount * kEnabledRegionsPerEye;
	static_assert(kPhysicalFeatureSlotCount <= std::numeric_limits<std::uint32_t>::digits);

	/** Higher-count qualification has not admitted smaller native rectangles. */
	template <class Plan>
	[[nodiscard]] constexpr bool QualifiedHigherRegionGeometry(const Plan& plan) noexcept
	{
		if (plan.count <= kDefaultRegionsPerEye)
			return true;
		if (plan.count > plan.regions.size())
			return false;
		for (std::uint32_t i = 0; i < plan.count; ++i)
			if (plan.regions[i].width < 128 || plan.regions[i].height < 128)
				return false;
		return true;
	}

	/** Invalid indices cannot wrap a shift into another physical slot. */
	[[nodiscard]] constexpr std::uint32_t FeatureSlotBit(std::uint32_t slot) noexcept
	{
		return slot < kPhysicalFeatureSlotCount ? std::uint32_t{ 1 } << slot : 0u;
	}

	/** Membership retains logical route/eye in each region's four-slot group. */
	[[nodiscard]] constexpr std::uint32_t RegionRouteMask(std::uint32_t logicalMask) noexcept
	{
		std::uint32_t result = 0;
		for (std::uint32_t region = 0; region < kEnabledRegionsPerEye; ++region)
			result |= (logicalMask & 0xFu) << (region * kLogicalFeatureSlotCount);
		return result;
	}

	[[nodiscard]] constexpr std::uint32_t LogicalRegionMask(std::uint32_t physicalMask) noexcept
	{
		std::uint32_t result = 0;
		for (std::uint32_t region = 0; region < kEnabledRegionsPerEye; ++region)
			result |= (physicalMask >> (region * kLogicalFeatureSlotCount)) & 0xFu;
		return result;
	}
}
