#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

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

	struct PrioritizedCharacterRegion
	{
		CharacterRect rect{};
		std::uint64_t priority = 0;
		std::uint32_t stableId = 0;
	};

	namespace CharacterRegionPolicy
	{
		inline constexpr std::uint32_t kNearbyRegionPixels = 8;

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

		[[nodiscard]] constexpr bool OverlapsOrNear(
			const CharacterRect& a_left,
			const CharacterRect& a_right,
			std::uint32_t a_nearbyPixels = kNearbyRegionPixels) noexcept
		{
			if (!a_left.IsValid() || !a_right.IsValid())
				return false;
			const auto saturatingAdd = [](std::uint32_t a_value, std::uint32_t a_add) {
				return a_value > std::numeric_limits<std::uint32_t>::max() - a_add ?
				           std::numeric_limits<std::uint32_t>::max() :
				           a_value + a_add;
			};
			return a_left.minX <= saturatingAdd(a_right.maxX, a_nearbyPixels) &&
			       a_right.minX <= saturatingAdd(a_left.maxX, a_nearbyPixels) &&
			       a_left.minY <= saturatingAdd(a_right.maxY, a_nearbyPixels) &&
			       a_right.minY <= saturatingAdd(a_left.maxY, a_nearbyPixels);
		}

		[[nodiscard]] constexpr bool IsWithinHoldWindow(
			std::uint32_t a_currentFrame,
			std::uint32_t a_lastEligibleFrame,
			std::uint32_t a_holdFrames) noexcept
		{
			return a_currentFrame >= a_lastEligibleFrame &&
			       a_currentFrame - a_lastEligibleFrame <= a_holdFrames;
		}

		/** Merges only nearby coverage, then drops low-priority regions at the cap. */
		inline std::uint32_t MergeAndLimit(
			std::vector<PrioritizedCharacterRegion>& a_regions,
			std::uint32_t a_maximumRegions,
			std::uint32_t a_nearbyPixels = kNearbyRegionPixels)
		{
			const auto canonicalLess = [](const auto& a_left, const auto& a_right) {
				if (a_left.rect.minY != a_right.rect.minY)
					return a_left.rect.minY < a_right.rect.minY;
				if (a_left.rect.minX != a_right.rect.minX)
					return a_left.rect.minX < a_right.rect.minX;
				if (a_left.rect.maxY != a_right.rect.maxY)
					return a_left.rect.maxY < a_right.rect.maxY;
				if (a_left.rect.maxX != a_right.rect.maxX)
					return a_left.rect.maxX < a_right.rect.maxX;
				return a_left.stableId < a_right.stableId;
			};
			a_regions.erase(
				std::remove_if(
					a_regions.begin(), a_regions.end(),
					[](const auto& a_region) { return !a_region.rect.IsValid(); }),
				a_regions.end());

			for (;;) {
				std::ranges::sort(a_regions, canonicalLess);
				std::size_t bestLeft = a_regions.size();
				std::size_t bestRight = a_regions.size();
				std::uint64_t bestInflation =
					std::numeric_limits<std::uint64_t>::max();
				for (std::size_t left = 0; left < a_regions.size(); ++left) {
					for (std::size_t right = left + 1; right < a_regions.size(); ++right) {
						if (!OverlapsOrNear(
								a_regions[left].rect, a_regions[right].rect,
								a_nearbyPixels)) {
							continue;
						}
						const auto combined = Union(
							a_regions[left].rect, a_regions[right].rect);
						const auto leftArea = a_regions[left].rect.Area();
						const auto rightArea = a_regions[right].rect.Area();
						const auto sourceArea =
							leftArea > std::numeric_limits<std::uint64_t>::max() - rightArea ?
								std::numeric_limits<std::uint64_t>::max() :
								leftArea + rightArea;
						const auto inflation = combined.Area() > sourceArea ?
						                           combined.Area() - sourceArea :
						                           0;
						if (inflation < bestInflation) {
							bestInflation = inflation;
							bestLeft = left;
							bestRight = right;
						}
					}
				}
				if (bestLeft == a_regions.size())
					break;
				a_regions[bestLeft] = {
					.rect = Union(
						a_regions[bestLeft].rect, a_regions[bestRight].rect),
					.priority = std::max(
						a_regions[bestLeft].priority,
						a_regions[bestRight].priority),
					.stableId = std::min(
						a_regions[bestLeft].stableId,
						a_regions[bestRight].stableId),
				};
				a_regions.erase(a_regions.begin() + bestRight);
			}

			const auto beforeLimit = a_regions.size();
			if (a_regions.size() > a_maximumRegions) {
				std::ranges::sort(a_regions, [](const auto& a_left, const auto& a_right) {
					if (a_left.priority != a_right.priority)
						return a_left.priority > a_right.priority;
					if (a_left.rect.Area() != a_right.rect.Area())
						return a_left.rect.Area() > a_right.rect.Area();
					return a_left.stableId < a_right.stableId;
				});
				a_regions.resize(a_maximumRegions);
			}
			std::ranges::sort(a_regions, canonicalLess);
			return static_cast<std::uint32_t>(beforeLimit - a_regions.size());
		}
	}
}
