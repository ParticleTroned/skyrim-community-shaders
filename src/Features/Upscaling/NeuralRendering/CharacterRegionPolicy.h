#pragma once

#include <algorithm>
#include <cstdint>

namespace NeuralRendering
{
	struct CharacterRect
	{
		std::uint32_t minX = 0;
		std::uint32_t minY = 0;
		std::uint32_t maxX = 0;
		std::uint32_t maxY = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return maxX > minX && maxY > minY;
		}

		[[nodiscard]] constexpr std::uint64_t Area() const noexcept
		{
			return IsValid() ?
			           static_cast<std::uint64_t>(maxX - minX) *
			               (maxY - minY) :
			           0;
		}

		bool operator==(const CharacterRect&) const = default;
	};

	namespace CharacterRegionPolicy
	{
		[[nodiscard]] constexpr CharacterRect Union(
			const CharacterRect& a_left,
			const CharacterRect& a_right) noexcept
		{
			if (!a_left.IsValid())
				return a_right;
			if (!a_right.IsValid())
				return a_left;
			return {
				.minX = std::min(a_left.minX, a_right.minX),
				.minY = std::min(a_left.minY, a_right.minY),
				.maxX = std::max(a_left.maxX, a_right.maxX),
				.maxY = std::max(a_left.maxY, a_right.maxY),
			};
		}

		[[nodiscard]] constexpr bool IsWithinHoldWindow(
			std::uint32_t a_currentFrame,
			std::uint32_t a_lastEligibleFrame,
			std::uint32_t a_holdFrames) noexcept
		{
			return a_currentFrame >= a_lastEligibleFrame &&
			       a_currentFrame - a_lastEligibleFrame <= a_holdFrames;
		}
	}
}
