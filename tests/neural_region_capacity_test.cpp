#include "Features/Upscaling/NeuralRendering/CharacterMaskRoi.h"
#include "Features/Upscaling/NeuralRendering/ColorMeasurementBatch.h"

#include <array>
#include <vector>

#define CHECK(condition)     \
	do {                     \
		if (!(condition))    \
			return __LINE__; \
	} while (false)

int main()
{
	using namespace NeuralRendering;
	static_assert(kPhysicalFeatureSlotCount == 32 && kMaximumRegionEvaluations == 16);
	static_assert(FeatureSlotBit(31) == 0x80000000u && FeatureSlotBit(32) == 0);
	static_assert(RegionRouteMask(3) == 0x33333333u && RegionRouteMask(12) == 0xccccccccu);
	constexpr std::array actors{
		CharacterMultiRoiActor{ 11, { 100, 100, 180, 200 } },
		CharacterMultiRoiActor{ 22, { 1600, 100, 1680, 200 } },
		CharacterMultiRoiActor{ 33, { 100, 1600, 180, 1700 } },
		CharacterMultiRoiActor{ 44, { 1600, 1600, 1680, 1700 } }
	};
	const auto resolve = [](std::span<const CharacterMultiRoiActor> input, unsigned frame,
							 StableCharacterMultiRoi& state, unsigned limit) {
		std::vector<CharacterRect> eligibility;
		for (const auto& actor : input) eligibility.push_back(actor.rect);
		CharacterMultiRoiReason reason;
		return ResolveCharacterMultiRoi(input, eligibility, 2048, 2048, frame, state, reason, false, {}, limit);
	};
	StableCharacterMultiRoi state;
	const auto four = resolve(actors, 1, state, 4);
	CHECK(four.count == 4);
	CHECK(QualifiedHigherRegionGeometry(four));
	auto tooSmall = four;
	tooSmall.regions[0].width = 64;
	CHECK(!QualifiedHigherRegionGeometry(tooSmall));
	tooSmall.count = 2;
	CHECK(QualifiedHigherRegionGeometry(tooSmall));
	CHECK(resolve(actors, 1, state, 4) == four);
	CHECK(GetCharacterRegionSubmissionViolation(0, four, CharacterRegionEnclosure(four), 2048, 2048, true).empty());
	auto invalid = four;
	invalid.regionSlots[3] = invalid.regionSlots[0];
	CHECK(!GetCharacterRegionSubmissionViolation(0, invalid, CharacterRegionEnclosure(four), 2048, 2048, true).empty());
	invalid = four;
	invalid.regions[3] = invalid.regions[0];
	CHECK(!GetCharacterRegionSubmissionViolation(0, invalid, CharacterRegionEnclosure(four), 2048, 2048, true).empty());
	{
		auto local = state;
		local.clusters[0].stable.provider = UnionCharacterComputeSubrect(four.regions[0], four.regions[1]);
		const auto repaired = resolve(actors, 2, local, 4);
		CHECK(repaired.count == 3);
		CHECK(repaired.regionSlots[1] == 2 && repaired.historyKeys[1] == four.historyKeys[2]);
		CHECK(repaired.regionSlots[2] == 3 && repaired.historyKeys[2] == four.historyKeys[3]);
		CHECK(GetCharacterRegionSubmissionViolation(0, repaired, CharacterRegionEnclosure(repaired), 2048, 2048, true).empty());
	}
	const auto removed = resolve(std::span(actors).subspan(1), 2, state, 4);
	CHECK(removed.count == 3);
	for (unsigned i = 0; i < removed.count; ++i) {
		CHECK(removed.regionSlots[i] == i + 1);
		CHECK(removed.historyKeys[i] == four.historyKeys[i + 1]);
	}
	const auto returned = resolve(actors, 3, state, 4);
	CHECK(returned.count == 4 && returned.historyKeys[0] != four.historyKeys[0]);
	for (unsigned i = 1; i < 4; ++i) CHECK(returned.historyKeys[i] == four.historyKeys[i]);
	state = {};
	CHECK(resolve(actors, 1, state, 2).count == 2);
	CHECK(resolve(actors, 2, state, 4).count == 4);
	CHECK(resolve(actors, 3, state, 1).count == 0);
	CHECK(resolve(actors, 4, state, 9).count == 0);

	std::vector<CharacterMaskRoiTileBounds> tiles(64 * 64);
	std::vector<CharacterRect> pieces;
	for (unsigned row = 0; row < 2; ++row)
		for (unsigned col = 0; col < 4; ++col) {
			const auto x = 96 + col * 512, y = 128 + row * 1280;
			pieces.push_back({ x, y, x + 16, y + 16 });
			tiles[(y / 32) * 64 + x / 32] = { x, y, x + 16, y + 16 };
		}
	StableCharacterMaskRoi mask;
	const std::array<std::uint64_t, 1> owner{ 77 };
	const auto eight = ResolveCharacterMaskRoi(tiles, owner, 2048, 2048, 10, mask, false, true, nullptr, nullptr, 8, 1);
	CHECK(eight.valid && eight.computeRegions.count == 8);
	CHECK(ResolveCharacterMaskRoi(tiles, owner, 2048, 2048, 10, mask, false, true, nullptr, nullptr, 8, 1) == eight);
	CHECK(GetCharacterRegionSubmissionViolation(2, eight.computeRegions, eight.computeSubrect, 2048, 2048, true).empty());
	for (const auto& piece : pieces) {
		const auto required = BuildCharacterComputeSubrect(std::span(&piece, 1), 2048, 2048);
		CHECK(std::ranges::any_of(std::span(eight.computeRegions.regions).first(8), [&](const auto& region) { return ContainsComputeSubrect(region, required); }));
	}
	StableCharacterMaskRoi peer;
	const auto other = ResolveCharacterMaskRoi(tiles, owner, 2048, 2048, 10, peer, false, true, nullptr, nullptr, 8, 2);
	CHECK(other.computeRegions.count == 8);
	CHECK(other.computeRegions.clusterIdentities != eight.computeRegions.clusterIdentities);
	const auto remapped = ResolveCharacterMaskRoi(tiles, owner, 2048, 2048, 11, mask, false, true, nullptr, nullptr, 8, 2);
	CHECK(remapped.computeRegions.clusterIdentities != eight.computeRegions.clusterIdentities);

	Color::MeasurementBatchKey key{ 1, 1, 1, 1, 1, 0, RegionRouteMask(12), true };
	CHECK(key.Valid());
	Color::MeasurementBatch<unsigned> batch;
	batch.key = key;
	for (unsigned i = 0; i < 32; ++i)
		if (key.expectedSlotMask & FeatureSlotBit(i))
			CHECK(batch.Record(key, i, i));
	CHECK(batch.Complete() && batch.samples[31] == 31);
	key.expectedSlotMask = FeatureSlotBit(14) | FeatureSlotBit(15);
	CHECK(key.Valid());
	const std::array<std::uint32_t, 4> required{ 0, 0, FeatureSlotBit(2) | FeatureSlotBit(30), FeatureSlotBit(3) | FeatureSlotBit(31) };
	CHECK(AggregateRegionEvaluationMask(0x8000000cu, required, false) == 12);
	CHECK(AggregateRegionEvaluationMask(0x8000000cu, required, true) == 8);
	const std::array previous{ SpatialRoiTrack{ { 10, 10, 30, 30 }, 101, true }, SpatialRoiTrack{ { 30, 10, 50, 30 }, 202, true } };
	const std::array joined{ CharacterRect{ 10, 10, 50, 30 } };
	const auto ambiguous = MatchSpatialRoiTracks(joined, previous, 1, 1);
	CHECK(!ambiguous[0].confident && ambiguous[0].identity != 101 && ambiguous[0].identity != 202);
	return 0;
}
