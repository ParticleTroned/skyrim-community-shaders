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
	using NeuralRendering::ResolveCharacterProviderRoiHeadroom;
	using NeuralRendering::ResolveStableCharacterComputeSubrect;
	using NeuralRendering::StableCharacterComputeSubrect;

	static_assert(!ComputeSubrect{}.IsValid());
	static_assert(!ComputeSubrect{}.Fits(100, 100));
	static_assert(ComputeSubrect{ 90, 80, 10, 20 }.Fits(100, 100));
	static_assert(!ComputeSubrect{ 90, 80, 11, 20 }.Fits(100, 100));
	static_assert(ComputeSubrect{ 2, 3, 4, 5 }.Area() == 20);
	static_assert(ResolveCharacterProviderRoiHeadroom(200) == 32);
	static_assert(ResolveCharacterProviderRoiHeadroom(640) == 53);
	static_assert(ResolveCharacterProviderRoiHeadroom(1512) == 126);
	static_assert(ResolveCharacterProviderRoiHeadroom(1680) == 128);

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
	if (initialProvider != ComputeSubrect{ 0, 0, 320, 384 } ||
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
	const auto expandedProvider = ResolveStableCharacterComputeSubrect(
		escaped, 1512, 1680, stable);
	if (expandedProvider != ComputeSubrect{ 0, 0, 512, 384 } ||
		expandedProvider.baseX != initialProvider.baseX ||
		expandedProvider.baseY != initialProvider.baseY ||
		!ContainsComputeSubrect(expandedProvider, escaped)) {
		return 10;
	}

	// A genuinely wider requirement may grow an extent. Alternating contained
	// candidates must not satisfy the stable-contraction delay.
	const ComputeSubrect widerRequired{ 100, 100, 500, 120 };
	const auto grownProvider = ResolveStableCharacterComputeSubrect(
		widerRequired, 1512, 1680, stable);
	if (grownProvider != ComputeSubrect{ 0, 0, 768, 384 } ||
		!ContainsComputeSubrect(grownProvider, expandedProvider)) {
		return 11;
	}
	for (std::uint32_t frame = 0;
		frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames * 2u;
		++frame) {
		const auto provider = ResolveStableCharacterComputeSubrect(
			(frame & 1u) ? initialRequired : escaped,
			1512, 1680, stable);
		if (provider != grownProvider) {
			return 12;
		}
	}
	if (stable.pendingShrink != initialProvider ||
		stable.shrinkCandidateFrames != 1) {
		return 13;
	}

	// Expansion must preserve the far edges when a nonzero provider grows left
	// and up. Moving back over the covered area must then leave it unchanged.
	StableCharacterComputeSubrect reverse{};
	const auto reverseInitial = ResolveStableCharacterComputeSubrect(
		{ 1000, 1000, 80, 120 }, 1512, 1680, reverse);
	if (reverseInitial != ComputeSubrect{ 832, 832, 384, 448 }) {
		return 14;
	}
	const auto reverseExpanded = ResolveStableCharacterComputeSubrect(
		{ 700, 700, 80, 120 }, 1512, 1680, reverse);
	if (reverseExpanded != ComputeSubrect{ 512, 512, 704, 768 } ||
		ResolveStableCharacterComputeSubrect(
			{ 1000, 1000, 80, 120 }, 1512, 1680, reverse) !=
			reverseExpanded) {
		return 15;
	}

	const ComputeSubrect largeRequired{ 128, 128, 800, 800 };
	const auto largeProvider = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, stable);
	if (largeProvider != ComputeSubrect{ 0, 0, 1088, 1088 } ||
		!ContainsComputeSubrect(largeProvider, grownProvider) ||
		!ContainsComputeSubrect(largeProvider, largeRequired)) {
		return 16;
	}
	const auto retainedLargeProvider = ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, stable);
	if (retainedLargeProvider.width != largeProvider.width ||
		retainedLargeProvider.height != largeProvider.height ||
		!ContainsComputeSubrect(retainedLargeProvider, initialRequired) ||
		!stable.pendingShrink.IsValid() ||
		stable.shrinkCandidateFrames != 1) {
		return 17;
	}

	StableCharacterComputeSubrect contraction{};
	const auto contractionLarge = ResolveStableCharacterComputeSubrect(
		largeRequired, 1512, 1680, contraction);
	for (std::uint32_t frame = 1;
		frame < NeuralRendering::kCharacterProviderRoiShrinkDelayFrames;
		++frame) {
		if (ResolveStableCharacterComputeSubrect(
				initialRequired, 1512, 1680, contraction) != contractionLarge) {
			return 18;
		}
	}
	const auto contractedProvider = ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, contraction);
	if (contractedProvider != initialProvider ||
		contraction.pendingShrink.IsValid() ||
		contraction.shrinkCandidateFrames != 0) {
		return 19;
	}

	StableCharacterComputeSubrect edge{};
	(void)ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, edge);
	const ComputeSubrect edgeRequired{ 1490, 1650, 22, 30 };
	const auto edgeProvider = ResolveStableCharacterComputeSubrect(
		edgeRequired, 1512, 1680, edge);
	if (edgeProvider != ComputeSubrect{ 0, 0, 1512, 1680 } ||
		!edgeProvider.Fits(1512, 1680) ||
		!ContainsComputeSubrect(edgeProvider, edgeRequired)) {
		return 20;
	}

	if (ResolveStableCharacterComputeSubrect(
			{}, 1512, 1680, stable).IsValid() ||
		stable.provider.IsValid() || stable.pendingShrink.IsValid() ||
		stable.shrinkCandidateFrames != 0) {
		return 21;
	}
	const auto reenteredProvider = ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, stable);
	if (reenteredProvider != initialProvider) {
		return 22;
	}
	return 0;
}
