#include "Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h"

#include <cstdint>
#include <limits>
#include <vector>

int main()
{
	using NeuralRendering::CharacterRect;
	using NeuralRendering::CharacterRegionCandidate;
	using NeuralRendering::CharacterRegionPolicy::CompactToCapacity;
	using NeuralRendering::CharacterRegionPolicy::CoveredArea;
	using NeuralRendering::CharacterRegionPolicy::IsWithinHoldWindow;
	using NeuralRendering::CharacterRegionPolicy::IsWithinMaximumDistance;
	using NeuralRendering::CharacterRegionPolicy::SelectAdaptive;
	using NeuralRendering::CharacterRegionPolicy::Union;

	static_assert(IsWithinHoldWindow(12, 10, 2));
	static_assert(!IsWithinHoldWindow(13, 10, 2));
	static_assert(!IsWithinHoldWindow(1, 0xFFFFFFFFu, 3));

	constexpr CharacterRect left{ 10, 20, 30, 40 };
	constexpr CharacterRect right{ 25, 5, 50, 35 };
	static_assert(Union(left, right) == CharacterRect{ 10, 5, 50, 40 });
	static_assert(Union({}, left) == left);
	static_assert(Union(right, {}) == right);
	static_assert(Union({}, {}) == CharacterRect{});
	if (!IsWithinMaximumDistance(30.0f, 0.0f) ||
		IsWithinMaximumDistance(10.1f, 10.0f) ||
		!IsWithinMaximumDistance(10.0f, 10.0f) ||
		IsWithinMaximumDistance(-1.0f, 0.0f) ||
		IsWithinMaximumDistance(1.0f, -1.0f) ||
		IsWithinMaximumDistance(
			1.0f, std::numeric_limits<float>::quiet_NaN())) {
		return 1;
	}
	if (!NeuralRendering::CharacterRegionPolicy::IsDetailRelevant(8.0f, 1) ||
		!NeuralRendering::CharacterRegionPolicy::IsDetailRelevant(9.0f, 96) ||
		NeuralRendering::CharacterRegionPolicy::IsDetailRelevant(9.0f, 95) ||
		NeuralRendering::CharacterRegionPolicy::IsDetailRelevant(-1.0f, 1000)) {
		return 2;
	}

	std::vector<CharacterRegionCandidate> candidates{
		{ { 200, 0, 220, 20 }, 12.0f, 64, 20 },
		{ { 2, 2, 18, 18 }, 14.0f, 64, 30 },
		{ { 0, 0, 20, 20 }, 4.0f, 64, 10 },
		{ { 300, 0, 320, 20 }, 15.0f, 128, 40 },
	};
	const auto culled = SelectAdaptive(candidates, true);
	if (culled != 2 || candidates.size() != 2 ||
		candidates[0].stableId != 10 || candidates[1].stableId != 40) {
		return 3;
	}

	std::vector<CharacterRegionCandidate> noDetailedActor{
		{ { 0, 0, 20, 20 }, 12.0f, 64, 1 },
	};
	if (SelectAdaptive(noDetailedActor, true) != 1 ||
		!noDetailedActor.empty()) {
		return 4;
	}

	std::vector<CharacterRegionCandidate> disabledCandidates{
		{ { 30, 0, 40, 10 }, 12.0f, 32, 3 },
		{ { 10, 0, 20, 10 }, 4.0f, 32, 2 },
		{ {}, 1.0f, 256, 1 },
	};
	if (SelectAdaptive(disabledCandidates, false) != 0 ||
		disabledCandidates.size() != 2 ||
		disabledCandidates[0].stableId != 2 ||
		disabledCandidates[1].stableId != 3) {
		return 5;
	}

	std::vector<CharacterRect> regions{
		{ 0, 0, 10, 10 },
		{ 11, 0, 21, 10 },
		{ 100, 100, 110, 110 },
	};
	CompactToCapacity(regions, 2);
	if (regions.size() != 2 ||
		std::ranges::none_of(regions, [](const auto& region) {
			return region.minX == 0 && region.maxX == 21;
		})) {
		return 6;
	}
	CompactToCapacity(regions, 0);
	if (!regions.empty())
		return 7;
	std::vector<CharacterRect> unlimitedCapacityRegions{
		{ 0, 0, 1, 1 },
	};
	CompactToCapacity(
		unlimitedCapacityRegions, std::numeric_limits<std::size_t>::max());
	if (unlimitedCapacityRegions.size() != 1)
		return 8;
	if (CoveredArea({
			{ 0, 0, 10, 10 },
			{ 5, 0, 15, 10 },
			{},
		}) != 150) {
		return 9;
	}
	return 0;
}
