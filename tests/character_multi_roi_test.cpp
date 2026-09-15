#include "Features/Upscaling/NeuralRendering/CharacterMultiRoi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#define CHECK(condition)     \
	do {                     \
		if (!(condition))    \
			return __LINE__; \
	} while (false)

namespace
{
	using namespace NeuralRendering;

	CharacterComputeRegionPlan Resolve(std::span<const CharacterMultiRoiActor> actors,
		std::uint32_t frame, StableCharacterMultiRoi& state, CharacterMultiRoiReason& reason,
		std::uint32_t width = 1536, std::uint32_t height = 1024,
		bool savingsGate = true, ComputeSubrect single = {})
	{
		std::vector<CharacterRect> eligibility;
		for (const auto& actor : actors)
			eligibility.push_back(actor.rect);
		return ResolveCharacterMultiRoi(actors, eligibility, width, height, frame, state, reason, savingsGate, single);
	}

	bool Safe(const CharacterComputeRegionPlan& plan, std::span<const CharacterMultiRoiActor> actors,
		std::uint32_t width = 1536, std::uint32_t height = 1024)
	{
		if (plan.count == 0)
			return plan == CharacterComputeRegionPlan{};
		if (plan.count != 2 || !plan.historyKeys[0] || !plan.historyKeys[1] ||
			plan.historyKeys[0] == plan.historyKeys[1] ||
			!plan.regions[0].Fits(width, height) || !plan.regions[1].Fits(width, height) ||
			CharacterComputeRegionsOverlap(plan.regions[0], plan.regions[1]))
			return false;
		for (const auto& actor : actors) {
			const auto required = BuildCharacterComputeSubrect(std::span(&actor.rect, 1), width, height);
			if (!ContainsComputeSubrect(plan.regions[0], required) &&
				!ContainsComputeSubrect(plan.regions[1], required))
				return false;
		}
		return true;
	}
}

int main()
{
	using namespace NeuralRendering;
	constexpr std::array separated{
		CharacterMultiRoiActor{ 11, { 100, 200, 200, 400 } },
		CharacterMultiRoiActor{ 22, { 1100, 200, 1200, 400 } },
	};
	// An additional invocation must pay its reserve before net savings count.
	const std::array marginalCost{
		ComputeSubrect{ 0, 0, 384, 1024 },
		ComputeSubrect{ 640, 0, 384, 1024 },
	};
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(marginalCost, 1024u * 1024u, false));
	const std::array enterCost{
		ComputeSubrect{ 0, 0, 352, 1024 },
		ComputeSubrect{ 672, 0, 352, 1024 },
	};
	CHECK(CharacterMultiRoiDetail::WorthSplitting(enterCost, 1024u * 1024u, false));
	const std::array retainCost{
		ComputeSubrect{ 0, 0, 376, 1024 },
		ComputeSubrect{ 648, 0, 376, 1024 },
	};
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(retainCost, 1024u * 1024u, false));
	CHECK(CharacterMultiRoiDetail::WorthSplitting(retainCost, 1024u * 1024u, true));
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(enterCost, 1024, false));
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(enterCost, 0, true));
	CHECK(CharacterMultiRoiDetail::WorthSplitting(marginalCost, 1024u * 1024u, false, false));
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(marginalCost, 768u * 1024u, false, false));
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(enterCost, 1024, false, false));
	// Check exact one-pixel boundaries, including ceil rounding for entry/retention.
	for (const bool retaining : { false, true }) {
		for (const std::uint64_t single : { 200000u, 200001u, 200003u, 200004u, 1048576u }) {
			const auto relative = retaining ? (single + 4) / 5 : (single + 3) / 4;
			const auto maximumSplit = single - 65536 - relative;
			std::array boundary{ ComputeSubrect{ 0, 0, 1, 1 },
				ComputeSubrect{ 2, 0, static_cast<std::uint32_t>(maximumSplit - 1), 1 } };
			const auto cost = CharacterMultiRoiDetail::CalculateSplitCost(boundary, single, retaining);
			CHECK(cost.valid && cost.splitPixels == maximumSplit && cost.savedPixels == 65536 + relative);
			CHECK(cost.requiredRelativePixels == relative && cost.meetsHeuristic);
			++boundary[1].width;
			CHECK(!CharacterMultiRoiDetail::WorthSplitting(boundary, single, retaining));
			CHECK(CharacterMultiRoiDetail::WorthSplitting(boundary, single, retaining, false));
		}
	}
	const auto maximum = std::numeric_limits<std::uint32_t>::max();
	const std::array overflowing{ ComputeSubrect{ 0, 0, maximum, maximum }, ComputeSubrect{ 0, 0, maximum, maximum } };
	CHECK(!CharacterMultiRoiDetail::CalculateSplitCost(overflowing, UINT64_MAX, false).valid);
	CHECK(!CharacterMultiRoiDetail::WorthSplitting(overflowing, UINT64_MAX, false, false));
	StableCharacterMultiRoi state;
	CharacterMultiRoiReason reason;
	const auto first = Resolve(separated, 100, state, reason);
	CHECK(first.count == 2 && reason == CharacterMultiRoiReason::Split);
	CHECK(Safe(first, separated));
	CHECK(first.regions[0].Area() + first.regions[1].Area() <
		  UnionCharacterComputeSubrect(first.regions[0], first.regions[1]).Area());
	const auto firstKey = first.historyKeys;
	const auto firstCount = state.clusters[0].stable.recentCount;
	for (int replay = 0; replay < 500; ++replay) {
		CHECK(Resolve(separated, 100, state, reason) == first);
		CHECK(state.clusters[0].stable.recentCount == firstCount);
	}
	// Canonical identity ordering, not observation order, owns the handle banks.
	auto reversed = separated;
	std::ranges::reverse(reversed);
	CHECK(Resolve(reversed, 100, state, reason) == first);
	CHECK(state.clusters[0].stable.recentCount == firstCount);
	StableCharacterMultiRoi otherEye;
	CHECK(Resolve(reversed, 100, otherEye, reason).historyKeys == first.historyKeys);
	otherEye = {};
	const auto laterEye = Resolve(reversed, 109, otherEye, reason);
	CHECK(laterEye.clusterIdentities == first.clusterIdentities);
	CHECK(laterEye.historyKeys != first.historyKeys);
	for (std::uint32_t frame = 101; frame < 700; ++frame) {
		auto moved = separated;
		for (auto& actor : moved) {
			actor.rect.minX += frame % 8;
			actor.rect.maxX += frame % 8;
		}
		const auto plan = Resolve(moved, frame, state, reason);
		CHECK(plan.count == 2 && Safe(plan, moved));
		CHECK(plan.historyKeys == firstKey);
		CHECK(plan.regions == first.regions);
	}
	// A same-source-frame reprepare must not consume a second stability sample.
	auto changed = separated;
	changed[0].rect.minX += 2;
	changed[0].rect.maxX += 2;
	const auto samples = state.clusters[0].stable.recentCursor;
	CHECK(Resolve(changed, 699, state, reason).count == 2);
	CHECK(state.clusters[0].stable.recentCursor == samples);
	CHECK(Resolve(separated, 699, state, reason).count == 2);
	CHECK(state.clusters[0].stable.recentCursor == samples);

	// Exact compacted eligibility remains a stronger constraint than actor bounds.
	const std::array bridge{ CharacterRect{ 100, 200, 1200, 400 } };
	CHECK(ResolveCharacterMultiRoi(separated, bridge, 1536, 1024, 700, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::EligibilityBridge);
	CHECK(ResolveCharacterMultiRoi(separated, bridge, 1536, 1024, 700, state, reason, false).count == 0);
	CHECK(reason == CharacterMultiRoiReason::EligibilityBridge);
	const auto reentry = Resolve(separated, 701, state, reason);
	CHECK(reentry.count == 2 && reentry.historyKeys != firstKey);
	CHECK(Safe(reentry, separated));

	// A newly visible actor in the gap must be covered immediately, including
	// reprepare of the same source frame; old cluster bounds cannot hide it.
	auto entered = std::vector<CharacterMultiRoiActor>(separated.begin(), separated.end());
	entered.push_back({ 33, { 620, 260, 750, 420 } });
	const auto entryPlan = Resolve(entered, 701, state, reason);
	CHECK(Safe(entryPlan, entered));
	CHECK(Resolve(entered, 701, state, reason) == entryPlan);

	// Offscreen/empty actors and invalid ownership never silently omit a region.
	CHECK(Resolve({}, 702, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::TooFewActors);
	CHECK(Resolve(std::span(separated).first(1), 703, state, reason).count == 0);
	auto invalid = separated;
	invalid[0].rect = {};
	CHECK(Resolve(invalid, 704, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::InvalidInput);
	invalid = separated;
	invalid[0].identity = 0;
	CHECK(Resolve(invalid, 705, state, reason).count == 0);
	invalid = separated;
	invalid[0].identity = invalid[1].identity;
	CHECK(Resolve(invalid, 706, state, reason).count == 0);
	invalid = separated;
	invalid[1].rect.maxX = 1537;
	CHECK(Resolve(invalid, 707, state, reason).count == 0);
	CHECK(Resolve(separated, 708, state, reason, 0, 1024).count == 0);
	std::vector<CharacterMultiRoiActor> crowded(129, separated[0]);
	CHECK(Resolve(crowded, 708, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::ActorCapacity);

	// Individually valid face rectangles are not enough: padding must be disjoint.
	const std::array close{
		CharacterMultiRoiActor{ 1, { 100, 200, 200, 400 } },
		CharacterMultiRoiActor{ 2, { 210, 200, 310, 400 } },
	};
	CHECK(Resolve(close, 709, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::NoDisjointSplit);
	CHECK(Resolve(close, 709, state, reason, 1536, 1024, false).count == 0);
	CHECK(reason == CharacterMultiRoiReason::NoDisjointSplit);
	CHECK(state.diagnostics.candidateOverlaps && !state.diagnostics.savingsGateEnabled);
	const std::array tiny{
		CharacterMultiRoiActor{ 1, { 10, 20, 20, 30 } },
		CharacterMultiRoiActor{ 2, { 200, 20, 210, 30 } },
	};
	CHECK(Resolve(tiny, 710, state, reason, 256, 128).count == 0);
	CHECK(reason == CharacterMultiRoiReason::InsufficientSavings);
	const auto ungatedTiny = Resolve(tiny, 710, state, reason, 256, 128, false);
	CHECK(ungatedTiny.count == 2 && Safe(ungatedTiny, tiny, 256, 128));
	CHECK(Resolve(tiny, 710, state, reason, 256, 128, true).count == 0);

	// Vertical separation must also pay the additional invocation reserve.
	auto vertical = std::array{
		CharacterMultiRoiActor{ 1, { 500, 20, 650, 170 } },
		CharacterMultiRoiActor{ 2, { 500, 800, 650, 950 } },
	};
	CHECK(Resolve(vertical, 711, state, reason).count == 0);
	CHECK(reason == CharacterMultiRoiReason::InsufficientSavings);
	vertical[1].rect.minY += 1000;
	vertical[1].rect.maxY += 1000;
	const auto verticalPlan = Resolve(vertical, 711, state, reason, 1536, 2048);
	CHECK(verticalPlan.count == 2 && Safe(verticalPlan, vertical, 1536, 2048));

	// Many actors are grouped, never reduced to the best two faces.
	const std::array groups{
		CharacterMultiRoiActor{ 10, { 100, 200, 200, 400 } },
		CharacterMultiRoiActor{ 11, { 140, 220, 240, 420 } },
		CharacterMultiRoiActor{ 20, { 1100, 200, 1200, 400 } },
		CharacterMultiRoiActor{ 21, { 1140, 220, 1240, 420 } },
	};
	const auto grouped = Resolve(groups, 712, state, reason);
	CHECK(grouped.count == 2 && Safe(grouped, groups));
	auto replacement = groups;
	replacement[1].identity = 12;
	const auto replaced = Resolve(replacement, 713, state, reason);
	CHECK(replaced.count == 2 && Safe(replaced, replacement));
	CHECK(replaced.historyKeys[0] != grouped.historyKeys[0]);
	CHECK(replaced.historyKeys[1] == grouped.historyKeys[1]);

	// The same actors crossing spatial order must retain ownership, not inherit
	// the history of whichever actor happens to become leftmost this frame.
	state = {};
	const auto beforeCross = Resolve(separated, 800, state, reason);
	auto crossed = separated;
	std::swap(crossed[0].rect, crossed[1].rect);
	const auto crossing = Resolve(crossed, 801, state, reason);
	CHECK(crossing.count == 0);  // Immediate growth of old providers overlaps: merge.
	CHECK(reason == CharacterMultiRoiReason::StableRegionsOverlap);
	const auto afterCross = Resolve(crossed, 802, state, reason);
	CHECK(afterCross.count == 2 && Safe(afterCross, crossed));
	CHECK(afterCross.historyKeys != beforeCross.historyKeys);
	CHECK(afterCross.regions[0].baseX > afterCross.regions[1].baseX);

	// Bounds arithmetic and frame wrap are valid at uint32 limits.
	constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
	const std::array huge{
		CharacterMultiRoiActor{ 1, { 0, 0, 1, 1 } },
		CharacterMultiRoiActor{ 2, { limit - 10, limit - 10, limit, limit } },
	};
	state = {};
	const auto hugePlan = Resolve(huge, limit, state, reason, limit, limit);
	CHECK(hugePlan.count == 2 && Safe(hugePlan, huge, limit, limit));
	CHECK(Resolve(huge, 0, state, reason, limit, limit).historyKeys == hugePlan.historyKeys);
	CHECK(Resolve(separated, 1, state, reason).count == 2);
	CHECK(state.width == 1536 && state.height == 1024);

	// Captured CSXTest01 eligibility bounds, with the actual retained fallback.
	const std::array faceLeft{
		CharacterMultiRoiActor{ 1, { 1056, 844, 1204, 964 } },
		CharacterMultiRoiActor{ 2, { 764, 856, 872, 976 } },
	};
	const std::array faceRight{
		CharacterMultiRoiActor{ 1, { 1040, 844, 1188, 964 } },
		CharacterMultiRoiActor{ 2, { 752, 856, 860, 976 } },
	};
	const ComputeSubrect faceFallback{ 640, 768, 704, 256 };
	for (const bool rightEye : { false, true }) {
		state = {};
		const auto& actors = rightEye ? faceRight : faceLeft;
		CHECK(Resolve(actors, 36633, state, reason, 1512, 1680, true, faceFallback).count == 0);
		CHECK(reason == CharacterMultiRoiReason::InsufficientSavings);
		const auto diagnostics = state.diagnostics;
		CHECK(diagnostics.cost.singlePixels == 180224);
		CHECK(diagnostics.cost.splitPixels == (rightEye ? 131072u : 147456u));
		CHECK(diagnostics.cost.savedPixels == (rightEye ? 49152u : 32768u));
		CHECK(diagnostics.cost.requiredRelativePixels == 45056);
		CHECK(!diagnostics.candidateOverlaps && diagnostics.candidateCoversEligibility);
		const auto plan = Resolve(actors, 36633, state, reason, 1512, 1680, false, faceFallback);
		CHECK(plan.count == 2 && Safe(plan, actors, 1512, 1680));
		CHECK(state.diagnostics.stabilizedCandidate && !state.diagnostics.cost.meetsHeuristic);
		CHECK(Resolve(actors, 36633, state, reason, 1512, 1680, true, faceFallback).count == 0);
		// Same-frame baseline changes must invalidate the cache and use the real cost.
		const ComputeSubrect largerFallback{ 512, 640, 896, 512 };
		CHECK(Resolve(actors, 36633, state, reason, 1512, 1680, true, largerFallback).count == 2);
		CHECK(state.diagnostics.cost.singlePixels == 458752);
		CHECK(Resolve(actors, 36633, state, reason, 1512, 1680, true, faceFallback).count == 0);
		CHECK(Resolve(actors, 36633, state, reason, 1512, 1680, false, { 0, 0, 10, 10 }).count == 0);
		CHECK(reason == CharacterMultiRoiReason::InvalidInput);
	}
	const std::array allLeft{
		CharacterMultiRoiActor{ 1, { 844, 780, 1512, 1624 } },
		CharacterMultiRoiActor{ 2, { 612, 828, 932, 1200 } },
	};
	const std::array allRight{
		CharacterMultiRoiActor{ 1, { 828, 780, 1512, 1624 } },
		CharacterMultiRoiActor{ 2, { 592, 828, 912, 1200 } },
	};
	for (const bool rightEye : { false, true }) {
		state = {};
		for (const bool gate : { true, false }) {
			CHECK(Resolve(rightEye ? allRight : allLeft, 38843, state, reason, 1512, 1680, gate,
					  { rightEye ? 448u : 512u, 640, rightEye ? 1064u : 1000u, 1040 })
					  .count == 0);
			CHECK(reason == CharacterMultiRoiReason::NoDisjointSplit);
			CHECK(state.diagnostics.candidateOverlaps && state.diagnostics.candidateAvailable);
			CHECK(state.diagnostics.cost.additionalPixels == (rightEye ? 28672u : 95232u));
			CHECK(state.diagnostics.overlappingCandidates == state.diagnostics.candidatesConsidered);
		}
	}

	// Deterministic adversarial motion, additions, occlusion and frame reuse.
	state = {};
	std::uint32_t random = 0x18C0FFEE;
	const auto nextRandom = [&]() {
		random ^= random << 13;
		random ^= random >> 17;
		random ^= random << 5;
		return random;
	};
	for (std::uint32_t frame = 0; frame < 4000; ++frame) {
		std::vector<CharacterMultiRoiActor> actors;
		for (std::uint32_t actor = 0, count = nextRandom() % 9; actor < count; ++actor) {
			const auto x = nextRandom() % 1300;
			const auto y = nextRandom() % 800;
			actors.push_back({ actor + 1u, { x, y, x + 20 + nextRandom() % 200, y + 20 + nextRandom() % 200 } });
		}
		const bool gate = frame % 2 == 0;
		const auto plan = Resolve(actors, frame, state, reason, 1536, 1024, gate);
		CHECK(Safe(plan, actors));
		CHECK(Resolve(actors, frame, state, reason, 1536, 1024, gate) == plan);
		std::ranges::reverse(actors);
		CHECK(Resolve(actors, frame, state, reason, 1536, 1024, gate) == plan);
	}
	return 0;
}
