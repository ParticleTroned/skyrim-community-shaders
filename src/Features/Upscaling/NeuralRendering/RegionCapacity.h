#pragma once
#include <cstdint>
namespace NeuralRendering
{
	inline constexpr std::uint32_t kLogicalFeatureSlotCount = 1;
	inline constexpr std::uint32_t kMinimumExperimentalContextExtent = 128;
	/** Native feature contexts retain the same qualified minimum on both axes. */
	template <class Rectangle>
	[[nodiscard]] constexpr bool QualifiedExperimentalContextGeometry(const Rectangle& rectangle) noexcept
	{
		return rectangle.width >= kMinimumExperimentalContextExtent && rectangle.height >= kMinimumExperimentalContextExtent;
	}
}
