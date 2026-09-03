#include "Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
	using NeuralRendering::CharacterRect;
	using NeuralRendering::PrioritizedCharacterRegion;

	bool Contains(
		const std::vector<PrioritizedCharacterRegion>& a_regions,
		const CharacterRect& a_rect)
	{
		return std::ranges::any_of(
			a_regions,
			[&](const auto& a_region) { return a_region.rect == a_rect; });
	}
}

int main()
{
	using NeuralRendering::CharacterRegionPolicy::IsWithinHoldWindow;
	using NeuralRendering::CharacterRegionPolicy::MergeAndLimit;
	static_assert(IsWithinHoldWindow(12, 10, 2));
	static_assert(!IsWithinHoldWindow(13, 10, 2));
	static_assert(!IsWithinHoldWindow(1, 0xFFFFFFFFu, 3));

	std::vector<PrioritizedCharacterRegion> nearby{
		{ { 10, 10, 20, 20 }, 100, 1 },
		{ { 25, 10, 35, 20 }, 80, 2 },
	};
	if (MergeAndLimit(nearby, 4) != 0 || nearby.size() != 1 ||
		nearby.front().rect != CharacterRect{ 10, 10, 35, 20 }) {
		return 1;
	}

	std::vector<PrioritizedCharacterRegion> separated{
		{ { 0, 0, 10, 10 }, 30, 3 },
		{ { 100, 0, 110, 10 }, 90, 1 },
		{ { 200, 0, 210, 10 }, 60, 2 },
	};
	if (MergeAndLimit(separated, 2) != 1 || separated.size() != 2 ||
		!Contains(separated, { 100, 0, 110, 10 }) ||
		!Contains(separated, { 200, 0, 210, 10 }) ||
		Contains(separated, { 0, 0, 210, 10 })) {
		return 2;
	}

	auto reversed = std::vector<PrioritizedCharacterRegion>{
		{ { 36, 8, 44, 16 }, 10, 4 },
		{ { 0, 8, 8, 16 }, 40, 1 },
		{ { 12, 8, 20, 16 }, 30, 2 },
	};
	auto forward = reversed;
	std::ranges::reverse(reversed);
	if (MergeAndLimit(forward, 2, 4) != MergeAndLimit(reversed, 2, 4) ||
		forward.size() != reversed.size()) {
		return 3;
	}
	for (std::size_t index = 0; index < forward.size(); ++index) {
		if (forward[index].rect != reversed[index].rect ||
			forward[index].priority != reversed[index].priority ||
			forward[index].stableId != reversed[index].stableId) {
			return 4;
		}
	}

	constexpr CharacterRect nearLimit{
		std::numeric_limits<std::uint32_t>::max() - 2u,
		0,
		std::numeric_limits<std::uint32_t>::max(),
		10,
	};
	constexpr CharacterRect adjacentLimit{
		std::numeric_limits<std::uint32_t>::max() - 1u,
		0,
		std::numeric_limits<std::uint32_t>::max(),
		10,
	};
	static_assert(NeuralRendering::CharacterRegionPolicy::OverlapsOrNear(
		nearLimit, adjacentLimit));
	return 0;
}
