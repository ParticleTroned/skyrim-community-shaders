#include "Features/Upscaling/NeuralRendering/ComputeSubrect.h"
#include "Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"

#include <array>
#include <limits>

int main()
{
	using NeuralRendering::BuildCenteredComputeSubrect;
	using NeuralRendering::BuildCharacterComputeSubrect;
	using NeuralRendering::ContainsComputeSubrect;
	using NeuralRendering::ComputeSubrect;
	using NeuralRendering::MapComputeSubrect;
	using NeuralRendering::ResolveStableCharacterComputeSubrect;
	using NeuralRendering::StableCharacterComputeSubrect;

	static_assert(!ComputeSubrect{}.IsValid());
	static_assert(!ComputeSubrect{}.Fits(100, 100));
	static_assert(ComputeSubrect{ 90, 80, 10, 20 }.Fits(100, 100));
	static_assert(!ComputeSubrect{ 90, 80, 11, 20 }.Fits(100, 100));
	static_assert(ComputeSubrect{ 2, 3, 4, 5 }.Area() == 20);

	static_assert(
		MapComputeSubrect({ 1, 1, 1, 1 }, 3, 3, 10, 10) ==
		ComputeSubrect{ 3, 3, 4, 4 });
	static_assert(
		MapComputeSubrect({ 7, 3, 3, 2 }, 10, 5, 3, 2) ==
		ComputeSubrect{ 2, 1, 1, 1 });
	static_assert(
		MapComputeSubrect({ 0, 0, 10, 5 }, 10, 5, 3, 2) ==
		ComputeSubrect{ 0, 0, 3, 2 });
	static_assert(!MapComputeSubrect({}, 10, 10, 20, 20).IsValid());
	static_assert(!MapComputeSubrect({ 0, 0, 1, 1 }, 0, 1, 1, 1).IsValid());

	constexpr std::array characterRegions{
		NeuralRendering::CharacterRect{ 13, 17, 29, 35 },
		NeuralRendering::CharacterRect{ 80, 70, 95, 99 },
	};
	if (BuildCharacterComputeSubrect(characterRegions, 100, 100) !=
		ComputeSubrect{ 8, 8, 92, 92 }) {
		return 1;
	}
	constexpr std::array fullRegion{
		NeuralRendering::CharacterRect{ 0, 0, 1512, 1680 },
	};
	if (BuildCharacterComputeSubrect(fullRegion, 1512, 1680) !=
		ComputeSubrect{ 0, 0, 1512, 1680 }) {
		return 2;
	}
	constexpr std::array invalidRegion{
		NeuralRendering::CharacterRect{ 90, 90, 101, 100 },
	};
	if (BuildCharacterComputeSubrect(invalidRegion, 100, 100).IsValid())
		return 3;

	if (BuildCenteredComputeSubrect(1512, 1680, 0.5f) !=
		ComputeSubrect{ 378, 420, 756, 840 }) {
		return 4;
	}
	if (BuildCenteredComputeSubrect(3, 3, 0.25f) !=
		ComputeSubrect{ 1, 1, 1, 1 }) {
		return 5;
	}
	if (BuildCenteredComputeSubrect(
			100, 80, std::numeric_limits<float>::quiet_NaN()) !=
		ComputeSubrect{ 0, 0, 100, 80 }) {
		return 6;
	}
	if (BuildCenteredComputeSubrect(0, 80, 1.0f).IsValid())
		return 7;

	StableCharacterComputeSubrect stable{};
	const ComputeSubrect initialRequired{ 100, 100, 80, 120 };
	const auto initialProvider = ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, stable);
	if (initialProvider != ComputeSubrect{ 64, 64, 192, 192 } ||
		!initialProvider.Fits(1512, 1680) ||
		!ContainsComputeSubrect(initialProvider, initialRequired)) {
		return 8;
	}
	for (std::uint32_t motion = 1; motion <= 8; ++motion) {
		const ComputeSubrect moved{
			initialRequired.baseX + motion,
			initialRequired.baseY + motion,
			initialRequired.width,
			initialRequired.height,
		};
		if (ResolveStableCharacterComputeSubrect(
				moved, 1512, 1680, stable) != initialProvider) {
			return 9;
		}
	}

	const ComputeSubrect escaped{ 300, 100, 80, 120 };
	const auto recenteredProvider = ResolveStableCharacterComputeSubrect(
		escaped, 1512, 1680, stable);
	if (recenteredProvider == initialProvider ||
		!ContainsComputeSubrect(recenteredProvider, escaped)) {
		return 10;
	}

	StableCharacterComputeSubrect contraction{};
	const ComputeSubrect largeRequired{ 128, 128, 800, 800 };
	const auto largeProvider = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, contraction);
	const ComputeSubrect smallRequired{ 384, 384, 64, 64 };
	for (std::uint32_t frame = 1;
		 frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames;
		 ++frame) {
		if (ResolveStableCharacterComputeSubrect(
				smallRequired, 1512, 1680, contraction) != largeProvider) {
			return 11;
		}
	}
	const auto contractedProvider = ResolveStableCharacterComputeSubrect(
		smallRequired, 1512, 1680, contraction);
	if (contractedProvider == largeProvider ||
		!ContainsComputeSubrect(contractedProvider, smallRequired)) {
		return 12;
	}

	if (ResolveStableCharacterComputeSubrect(
			{}, 1512, 1680, contraction).IsValid() ||
		contraction.provider.IsValid() || contraction.pendingShrink.IsValid() ||
		contraction.shrinkCandidateFrames != 0) {
		return 13;
	}
	const auto reenteredProvider = ResolveStableCharacterComputeSubrect(
		smallRequired, 1512, 1680, contraction);
	if (!ContainsComputeSubrect(reenteredProvider, smallRequired) ||
		reenteredProvider == largeProvider) {
		return 14;
	}

	StableCharacterComputeSubrect movingContraction{};
	const auto movingLargeProvider = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, movingContraction);
	const std::array movingSmallRequired{
		ComputeSubrect{ 320, 384, 64, 64 },
		ComputeSubrect{ 448, 384, 64, 64 },
	};
	for (std::uint32_t frame = 0;
		 frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames * 2u;
		 ++frame) {
		if (ResolveStableCharacterComputeSubrect(
				movingSmallRequired[frame & 1u], 1512, 1680,
				movingContraction) != movingLargeProvider) {
			return 15;
		}
	}

	StableCharacterComputeSubrect edge{};
	const ComputeSubrect edgeRequired{ 1490, 1650, 22, 30 };
	const auto edgeProvider = ResolveStableCharacterComputeSubrect(
		edgeRequired, 1512, 1680, edge);
	if (edgeProvider != ComputeSubrect{ 1408, 1600, 104, 80 } ||
		!edgeProvider.Fits(1512, 1680) ||
		!ContainsComputeSubrect(edgeProvider, edgeRequired)) {
		return 16;
	}

	StableCharacterComputeSubrect interruptedContraction{};
	const auto interruptedLarge = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, interruptedContraction);
	for (std::uint32_t frame = 1;
		 frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames;
		 ++frame) {
		if (ResolveStableCharacterComputeSubrect(
				smallRequired, 1512, 1680, interruptedContraction) !=
			interruptedLarge) {
			return 17;
		}
	}
	(void)ResolveStableCharacterComputeSubrect(
		{}, 1512, 1680, interruptedContraction);
	const auto restartedProvider = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, interruptedContraction);
	for (std::uint32_t frame = 1;
		 frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames;
		 ++frame) {
		if (ResolveStableCharacterComputeSubrect(
				smallRequired, 1512, 1680, interruptedContraction) !=
			restartedProvider) {
			return 18;
		}
	}
	return 0;
}
