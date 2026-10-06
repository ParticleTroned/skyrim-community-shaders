#include "Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"
#include "Features/Upscaling/NeuralRendering/ComputeSubrect.h"

#include <array>
#include <limits>

int main()
{
	using NeuralRendering::BuildCenteredComputeSubrect;
	using NeuralRendering::BuildCharacterComputeSubrect;
	using NeuralRendering::ComputeSubrect;
	using NeuralRendering::ContainsComputeSubrect;
	using NeuralRendering::MapComputeSubrect;
	using NeuralRendering::ResolveCharacterProviderRoiHeadroom;
	using NeuralRendering::ResolveStableCharacterComputeSubrect;
	using NeuralRendering::StableCharacterComputeSubrect;

	static_assert(!ComputeSubrect{}.IsValid());
	static_assert(!ComputeSubrect{}.Fits(100, 100));
	static_assert(ComputeSubrect{ 90, 80, 10, 20 }.Fits(100, 100));
	static_assert(!ComputeSubrect{ 90, 80, 11, 20 }.Fits(100, 100));
	static_assert(ComputeSubrect{ 2, 3, 4, 5 }.Area() == 20);
	static_assert(ResolveCharacterProviderRoiHeadroom(100) == 32);
	static_assert(ResolveCharacterProviderRoiHeadroom(200) == 41);
	static_assert(ResolveCharacterProviderRoiHeadroom(640) == 96);
	static_assert(ResolveCharacterProviderRoiHeadroom(1680) == 96);

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
	const auto expandedProvider = ResolveStableCharacterComputeSubrect(
		escaped, 1512, 1680, stable);
	if (expandedProvider != ComputeSubrect{ 64, 64, 384, 192 } ||
		expandedProvider.baseX != initialProvider.baseX ||
		expandedProvider.baseY != initialProvider.baseY ||
		!ContainsComputeSubrect(expandedProvider, escaped)) {
		return 10;
	}

	// Expansion must preserve the far edges when a nonzero provider grows left
	// and up. Moving back over the covered area must then leave it unchanged.
	StableCharacterComputeSubrect reverse{};
	const auto reverseInitial = ResolveStableCharacterComputeSubrect(
		{ 1000, 1000, 80, 120 }, 1512, 1680, reverse);
	if (!ContainsComputeSubrect(reverseInitial, { 1000, 1000, 80, 120 })) {
		return 11;
	}
	const auto reverseExpanded = ResolveStableCharacterComputeSubrect(
		{ 700, 700, 80, 120 }, 1512, 1680, reverse);
	if (!ContainsComputeSubrect(reverseExpanded, reverseInitial) ||
		!ContainsComputeSubrect(reverseExpanded, { 700, 700, 80, 120 }) ||
		ResolveStableCharacterComputeSubrect(
			{ 1000, 1000, 80, 120 }, 1512, 1680, reverse) !=
			reverseExpanded) {
		return 12;
	}

	// A transient full-eye requirement expires even when one projected pixel
	// alternates across the production four/eight-pixel alignment boundaries.
	StableCharacterComputeSubrect contraction{};
	const ComputeSubrect fullEye{ 0, 0, 1512, 1680 };
	const auto contractionLarge = ResolveStableCharacterComputeSubrect(
		fullEye, 1512, 1680, contraction);
	ComputeSubrect oscillationUnion{};
	for (std::uint32_t frame = 1;
		frame <= NeuralRendering::kCharacterProviderRoiHistoryFrames;
		++frame) {
		const std::uint32_t projectedX = (frame & 1u) ? 644u : 643u;
		const std::array regions{ NeuralRendering::CharacterRect{
			projectedX / 4u * 4u, 780,
			(projectedX + 103u) / 4u * 4u, 880 } };
		const auto required = BuildCharacterComputeSubrect(regions, 1512, 1680);
		oscillationUnion = NeuralRendering::UnionCharacterComputeSubrect(
			oscillationUnion, required);
		const auto provider = ResolveStableCharacterComputeSubrect(
			required, 1512, 1680, contraction);
		if (!ContainsComputeSubrect(provider, required))
			return 13;
		if (frame < NeuralRendering::kCharacterProviderRoiHistoryFrames &&
			provider != contractionLarge) {
			return 14;
		}
	}
	const auto contractedProvider = contraction.provider;
	if (contractedProvider.Area() > fullEye.Area() / 10u ||
		!ContainsComputeSubrect(contractedProvider, oscillationUnion) ||
		contraction.framesSinceContraction != 0) {
		return 15;
	}
	// Repeated contained oscillation must not trade the old cost bug for a
	// periodically shifting provider/history identity.
	for (std::uint32_t frame = 0; frame < 10000; ++frame) {
		const auto required = ComputeSubrect{
			(frame & 1u) ? 640u : 632u, 776, 120, 112
		};
		if (ResolveStableCharacterComputeSubrect(
				required, 1512, 1680, contraction) != contractedProvider) {
			return 16;
		}
	}

	// Continuous travel, including reversals, must neither starve contraction
	// through frequent growth nor retain the entire historical screen path.
	StableCharacterComputeSubrect moving{};
	for (std::uint32_t frame = 0; frame < 6000; ++frame) {
		const auto phase = frame % 600u;
		const auto offset = phase < 300u ? phase * 4u : (600u - phase) * 4u;
		const ComputeSubrect required{ 100u + offset, 700, 100, 100 };
		const auto provider = ResolveStableCharacterComputeSubrect(
			required, 1512, 1680, moving);
		if (!provider.Fits(1512, 1680) ||
			!ContainsComputeSubrect(provider, required) ||
			provider.width > 768u || provider.height > 256u) {
			return 17;
		}
	}

	// An edge-to-edge excursion can grow to almost the entire eye immediately,
	// but stale bounds are reclaimed while the latest edge remains covered.
	StableCharacterComputeSubrect edge{};
	(void)ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, edge);
	const ComputeSubrect edgeRequired{ 1490, 1650, 22, 30 };
	const auto edgeProvider = ResolveStableCharacterComputeSubrect(
		edgeRequired, 1512, 1680, edge);
	if (!edgeProvider.Fits(1512, 1680) ||
		!ContainsComputeSubrect(edgeProvider, initialRequired) ||
		!ContainsComputeSubrect(edgeProvider, edgeRequired)) {
		return 18;
	}
	for (std::uint32_t frame = 0;
		frame < NeuralRendering::kCharacterProviderRoiHistoryFrames;
		++frame) {
		(void)ResolveStableCharacterComputeSubrect(
			edgeRequired, 1512, 1680, edge);
	}
	if (edge.provider.Area() >= edgeProvider.Area() / 4u ||
		!ContainsComputeSubrect(edge.provider, edgeRequired)) {
		return 19;
	}

	if (ResolveStableCharacterComputeSubrect(
			{}, 1512, 1680, stable)
			.IsValid() ||
		stable.provider.IsValid() || stable.recentCount != 0 ||
		stable.framesSinceContraction != 0) {
		return 20;
	}
	const auto reenteredProvider = ResolveStableCharacterComputeSubrect(
		initialRequired, 1512, 1680, stable);
	if (reenteredProvider != initialProvider) {
		return 21;
	}
	// Even a growing eye extent starts a new coordinate/resource epoch rather
	// than silently retaining a still-in-bounds old envelope or its ring.
	const auto resizedProvider = ResolveStableCharacterComputeSubrect(
		{ 200, 200, 100, 100 }, 1600, 1800, stable);
	if (stable.recentCount != 1 || stable.width != 1600 || stable.height != 1800 ||
		resizedProvider != NeuralRendering::BuildCharacterProviderComputeSubrect(
							   { 200, 200, 100, 100 }, 1600, 1800)) {
		return 22;
	}
	if (ResolveStableCharacterComputeSubrect(
			{ 1590, 1790, 20, 20 }, 1600, 1800, stable)
			.IsValid() ||
		stable.recentCount != 0 || stable.provider.IsValid()) {
		return 23;
	}
	const ComputeSubrect centralSmall{ 706, 790, 100, 100 };
	const auto centralProvider = NeuralRendering::BuildCharacterProviderComputeSubrect(
		centralSmall, 1512, 1680);
	if (centralProvider != ComputeSubrect{ 640, 704, 256, 256 } ||
		centralProvider != NeuralRendering::BuildCharacterProviderComputeSubrect(
							   centralSmall, 3024, 3360)) {
		return 24;
	}

	// Adversarial histories and changing surface sizes cannot lose either this
	// frame's pixels or any still-retained recent requirement at contraction.
	StableCharacterComputeSubrect assorted{};
	std::uint32_t sequence = 0x4F49524Eu;
	const auto next = [&sequence]() {
		sequence = sequence * 1664525u + 1013904223u;
		return sequence;
	};
	for (std::uint32_t frame = 0; frame < 10000; ++frame) {
		const auto width = (frame / 137u) % 2u ? 1512u : 800u;
		const auto height = (frame / 137u) % 2u ? 1680u : 600u;
		const auto boxWidth = 1u + next() % 400u;
		const auto boxHeight = 1u + next() % 400u;
		const ComputeSubrect required{
			next() % (width - boxWidth + 1u),
			next() % (height - boxHeight + 1u), boxWidth, boxHeight
		};
		const auto provider = ResolveStableCharacterComputeSubrect(
			required, width, height, assorted);
		if (!provider.Fits(width, height) ||
			!ContainsComputeSubrect(provider, required)) {
			return 25;
		}
		for (std::uint32_t index = 0; index < assorted.recentCount; ++index) {
			if (!ContainsComputeSubrect(provider, assorted.recentRequired[index]))
				return 26;
		}
	}
	return 0;
}
