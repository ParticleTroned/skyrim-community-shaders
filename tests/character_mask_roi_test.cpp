#include "Features/Upscaling/NeuralRendering/CharacterEarlyMaskBounds.h"
#include "Features/Upscaling/NeuralRendering/CharacterMaskRoi.h"
#include "Features/Upscaling/NeuralRendering/CharacterMaskRoiAdmission.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#define CHECK(condition)     \
	do {                     \
		if (!(condition))    \
			return __LINE__; \
	} while (false)

namespace
{
	using namespace NeuralRendering;
	constexpr std::uint32_t kWidth = 1536;
	constexpr std::uint32_t kHeight = 1024;
	constexpr std::array<std::uint64_t, 2> kOwners{ 101, 202 };

	std::vector<CharacterMaskRoiTileBounds> Tiles(std::span<const CharacterRect> rectangles,
		std::uint32_t width = kWidth, std::uint32_t height = kHeight)
	{
		const auto columns = (width + 31u) / 32u;
		const auto rows = (height + 31u) / 32u;
		std::vector<CharacterMaskRoiTileBounds> result(columns * rows);
		for (std::uint32_t y = 0; y < rows; ++y) {
			for (std::uint32_t x = 0; x < columns; ++x) {
				CharacterRect occupied{};
				for (const auto& rect : rectangles) {
					const CharacterRect clipped{
						std::max(x * 32u, rect.minX),
						std::max(y * 32u, rect.minY),
						std::min({ (x + 1u) * 32u, width, rect.maxX }),
						std::min({ (y + 1u) * 32u, height, rect.maxY }),
					};
					if (clipped.IsValid())
						occupied = CharacterRegionPolicy::Union(occupied, clipped);
				}
				result[y * columns + x] = { occupied.minX, occupied.minY, occupied.maxX, occupied.maxY };
			}
		}
		return result;
	}

	bool Covered(const CharacterMaskRoiResult& result, std::span<const CharacterMaskRoiTileBounds> tiles,
		std::uint32_t width = kWidth, std::uint32_t height = kHeight)
	{
		if (!result.valid)
			return false;
		if (result.computeRegions.count == 2 &&
			CharacterComputeRegionsOverlap(result.computeRegions.regions[0], result.computeRegions.regions[1]))
			return false;
		for (const auto& tile : tiles) {
			if (tile == CharacterMaskRoiTileBounds{})
				continue;
			const CharacterRect rect{ tile.minX, tile.minY, tile.maxX, tile.maxY };
			const auto required = BuildCharacterComputeSubrect(std::span(&rect, 1), width, height);
			if (result.empty || !ContainsComputeSubrect(result.computeSubrect, required))
				return false;
			if (result.computeRegions.count == 2 &&
				!ContainsComputeSubrect(result.computeRegions.regions[0], required) &&
				!ContainsComputeSubrect(result.computeRegions.regions[1], required))
				return false;
		}
		return true;
	}
}

int main()
{
	using namespace NeuralRendering;
	// Readback attempts do not immediately toggle the tight provider path.
	// Initial admission requires three distinct forward fresh frames.
	CharacterMaskRoiAdmission admission;
	CHECK(admission.CanAttempt(10));
	CHECK(!admission.ObserveFresh(10));
	CHECK(!admission.ObserveFresh(10));
	CHECK(!admission.ObserveFresh(11));
	CHECK(admission.ObserveFresh(12));
	CHECK(admission.ObserveFresh(12));
	CHECK(admission.ObserveFresh(13));
	// Retained-world menus can skip frames without an explicit failure.
	CHECK(admission.ObserveFresh(200));
	CHECK(admission.ObserveFresh(201));
	// Backwards epochs cannot inherit established or partially warm histories.
	CHECK(!admission.ObserveFresh(100));
	CHECK(!admission.ObserveFresh(101));
	CHECK(admission.ObserveFresh(102));
	admission.Reject(103);
	CHECK(!admission.CanAttempt(103));
	CHECK(!admission.CanAttempt(132));
	CHECK(!admission.ObserveFresh(132));
	CHECK(admission.CanAttempt(133));
	CHECK(!admission.ObserveFresh(133));
	CHECK(!admission.ObserveFresh(134));
	CHECK(admission.ObserveFresh(135));
	// Skipped renderer IDs without a failed read must not prevent warmup;
	// explicit readback failures still reset admission and begin cooldown.
	admission = {};
	CHECK(!admission.ObserveFresh(1));
	CHECK(!admission.ObserveFresh(3));
	CHECK(!admission.ObserveFresh(3));
	CHECK(admission.ObserveFresh(4));
	CHECK(admission.ObserveFresh(5));
	admission = {};
	CHECK(!admission.ObserveFresh(10));
	CHECK(!admission.ObserveFresh(20));
	CHECK(!admission.ObserveFresh(19));
	CHECK(!admission.ObserveFresh(21));
	CHECK(admission.ObserveFresh(23));
	for (std::uint32_t episode = 0; episode < 8; ++episode) {
		const auto frame = 10u + episode * 40u;
		admission.Reject(frame);
		CHECK(!admission.CanAttempt(frame + 29u));
		CHECK(admission.CanAttempt(frame + 30u));
		CHECK(!admission.ObserveFresh(frame + 30u));
		admission.Reject(frame + 31u);
		CHECK(!admission.ObserveFresh(frame + 32u));
	}
	// Frame arithmetic crosses UINT32_MAX normally, both in warmup and retry.
	admission = {};
	CHECK(!admission.ObserveFresh(UINT32_MAX - 1u));
	CHECK(!admission.ObserveFresh(UINT32_MAX));
	CHECK(admission.ObserveFresh(0));
	CHECK(admission.ObserveFresh(15));
	admission.Reject(UINT32_MAX - 10u);
	CHECK(!admission.CanAttempt(UINT32_MAX));
	CHECK(!admission.CanAttempt(18));
	CHECK(admission.CanAttempt(19));
	CHECK(!admission.ObserveFresh(19));
	CHECK(!admission.ObserveFresh(20));
	CHECK(admission.ObserveFresh(21));
	// A backwards clock during a failure cooldown does not bypass it; its
	// owner resets the policy alongside a real session/generation reset.
	admission.Reject(100);
	CHECK(!admission.CanAttempt(99));
	admission = {};
	CHECK(admission.CanAttempt(0));
	// Oversized projected mesh/eligibility boxes overlap, but actual heads,
	// hair and disconnected exposed hands have a large, proven empty gap.
	const std::array cpuActors{
		CharacterMultiRoiActor{ 101, { 64, 64, 1100, 900 } },
		CharacterMultiRoiActor{ 202, { 500, 64, 1472, 960 } },
	};
	const std::array cpuEligibility{ cpuActors[0].rect, cpuActors[1].rect };
	StableCharacterMultiRoi cpuState;
	CharacterMultiRoiReason cpuReason;
	CHECK(ResolveCharacterMultiRoi(cpuActors, cpuEligibility, kWidth, kHeight, 1, cpuState, cpuReason).count == 0);
	const std::array selected{
		CharacterRect{ 150, 200, 310, 380 },
		CharacterRect{ 100, 430, 140, 470 },
		CharacterRect{ 320, 440, 360, 480 },
		CharacterRect{ 1100, 160, 1210, 350 },
		CharacterRect{ 1050, 450, 1090, 510 },
		CharacterRect{ 1240, 500, 1270, 550 },
		CharacterRect{ 1180, 600, 1220, 790 },
	};
	auto tiles = Tiles(selected);
	StableCharacterMaskRoi state;
	const auto first = ResolveCharacterMaskRoi(tiles, kOwners, kWidth, kHeight, 100, state);
	CHECK(first.valid && !first.empty && first.computeRegions.count == 2);
	CHECK(first.multiRoiReason == CharacterMultiRoiReason::Split && first.splitAxis == 0);
	CHECK(Covered(first, tiles));
	CHECK(first.computeRegions.regions[0].Area() + first.computeRegions.regions[1].Area() < first.computeSubrect.Area());
	CHECK(first.requiredSubrect.Area() < BuildCharacterComputeSubrect(cpuEligibility, kWidth, kHeight).Area());
	const auto initialKey = first.computeRegions.historyKeys;
	const auto initialCount = state.multi.clusters[0].stable.recentCount;
	const std::array<std::uint64_t, 2> reversedOwners{ 202, 101 };
	for (int replay = 0; replay < 100; ++replay) {
		CHECK(ResolveCharacterMaskRoi(tiles, reversedOwners, kWidth, kHeight, 100, state) == first);
		CHECK(state.multi.clusters[0].stable.recentCount == initialCount);
	}

	// Subpixel-sized hair/skin support is positive coverage, not a density
	// threshold. Disconnected support is retained even when far from a face.
	auto thin = std::vector<CharacterRect>(selected.begin(), selected.end());
	thin.push_back({ 70, 200, 71, 201 });
	thin.push_back({ 365, 670, 366, 671 });
	auto thinTiles = Tiles(thin);
	const auto thinResult = ResolveCharacterMaskRoi(thinTiles, kOwners, kWidth, kHeight, 101, state);
	CHECK(Covered(thinResult, thinTiles));
	CHECK(thinResult.computeRegions.count == 2 && thinResult.computeRegions.historyKeys == initialKey);

	// New current support grows immediately; old rectangles cannot clip it.
	for (std::uint32_t frame = 102; frame < 200; ++frame) {
		auto moved = selected;
		for (auto& rect : moved) {
			rect.minX += frame % 12;
			rect.maxX += frame % 12;
		}
		auto movedTiles = Tiles(moved);
		const auto result = ResolveCharacterMaskRoi(movedTiles, kOwners, kWidth, kHeight, frame, state);
		CHECK(Covered(result, movedTiles));
		CHECK(result.computeRegions.count == 2 && result.computeRegions.historyKeys == initialKey);
	}
	// No policy call during readback timeout: retain history, never make a
	// cached-coverage assertion. The next fresh frame remains fully covered.
	const auto afterGap = ResolveCharacterMaskRoi(tiles, kOwners, kWidth, kHeight, 250, state);
	CHECK(Covered(afterGap, tiles));
	CHECK(afterGap.computeRegions.historyKeys == initialKey);

	// Stereo disparity may move mask support; cluster identities remain
	// selected-owner/axis/side based, independent of eye or first-ready frame.
	auto stereo = selected;
	for (auto& rect : stereo) {
		rect.minX += 19;
		rect.maxX += 19;
	}
	StableCharacterMaskRoi otherEye;
	auto otherTiles = Tiles(stereo);
	const auto other = ResolveCharacterMaskRoi(otherTiles, reversedOwners, kWidth, kHeight, 105, otherEye);
	CHECK(other.computeRegions.count == 2 && Covered(other, otherTiles));
	CHECK(other.computeRegions.clusterIdentities == first.computeRegions.clusterIdentities);
	CHECK(other.computeRegions.historyKeys != first.computeRegions.historyKeys);

	// Actual mask support bridging the characters must not be dropped simply
	// to keep two regions. Fall back to a safe, tight single mask enclosure.
	const std::array bridge{ CharacterRect{ 100, 200, 1270, 790 } };
	auto bridgeTiles = Tiles(bridge);
	const auto bridged = ResolveCharacterMaskRoi(bridgeTiles, kOwners, kWidth, kHeight, 251, state);
	CHECK(bridged.computeRegions.count == 0 && Covered(bridged, bridgeTiles));
	CHECK(bridged.multiRoiReason == CharacterMultiRoiReason::NoDisjointSplit);

	// Also split vertically; identity must change with the spatial axis.
	const std::array stacked{
		CharacterRect{ 200, 30, 500, 140 }, CharacterRect{ 200, 850, 500, 960 }
	};
	auto stackedTiles = Tiles(stacked);
	StableCharacterMaskRoi stackedState;
	const auto stackedResult = ResolveCharacterMaskRoi(stackedTiles, kOwners, kWidth, kHeight, 100, stackedState);
	CHECK(stackedResult.computeRegions.count == 2 && stackedResult.splitAxis == 1);
	CHECK(Covered(stackedResult, stackedTiles));
	CHECK(stackedResult.computeRegions.clusterIdentities != first.computeRegions.clusterIdentities);

	// A same-frame changed input resets history rather than double-aging it.
	const auto reprepare = ResolveCharacterMaskRoi(tiles, kOwners, kWidth, kHeight, 251, state);
	CHECK(Covered(reprepare, tiles));
	CHECK(state.multi.clusters[0].stable.recentCount == 1);
	const std::array<std::uint64_t, 2> changedOwners{ 101, 303 };
	const auto changed = ResolveCharacterMaskRoi(tiles, changedOwners, kWidth, kHeight, 252, state);
	CHECK(changed.computeRegions.count == 2 && Covered(changed, tiles));
	CHECK(changed.computeRegions.clusterIdentities != first.computeRegions.clusterIdentities);

	const auto emptyTiles = Tiles({});
	const auto empty = ResolveCharacterMaskRoi(emptyTiles, kOwners, kWidth, kHeight, 253, state);
	CHECK(empty.valid && empty.empty && empty.occupiedTiles == 0);
	CHECK(!empty.computeSubrect.IsValid() && !empty.requiredSubrect.IsValid() && empty.computeRegions.count == 0);
	CHECK(state.multi.clusters[0].owners.empty() && !state.single.provider.IsValid());
	const auto returned = ResolveCharacterMaskRoi(tiles, kOwners, kWidth, kHeight, 254, state);
	CHECK(returned.computeRegions.count == 2 && returned.computeRegions.historyKeys != initialKey);

	// One actor still gets tight current-mask support, but no synthetic split.
	const std::array<std::uint64_t, 1> oneOwner{ 101 };
	CHECK(ResolveCharacterMaskRoi(tiles, oneOwner, kWidth, kHeight, 255, state).computeRegions.count == 0);
	CHECK(ResolveCharacterMaskRoi(tiles, {}, kWidth, kHeight, 256, state).valid);

	// Exact row-major ABI, including partial edge tiles, fails closed.
	const std::array edge{ CharacterRect{ 64, 32, 65, 33 } };
	auto edgeTiles = Tiles(edge, 65, 33);
	CHECK(edgeTiles.size() == 6);
	CHECK(Covered(ResolveCharacterMaskRoi(edgeTiles, kOwners, 65, 33, 300, state), edgeTiles, 65, 33));
	auto invalidTiles = edgeTiles;
	invalidTiles.back().maxX = 66;
	CHECK(!ResolveCharacterMaskRoi(invalidTiles, kOwners, 65, 33, 301, state).valid);
	invalidTiles = edgeTiles;
	invalidTiles.back().minY = 31;
	CHECK(!ResolveCharacterMaskRoi(invalidTiles, kOwners, 65, 33, 302, state).valid);
	invalidTiles = edgeTiles;
	invalidTiles[0] = { 1, 0, 0, 0 };
	CHECK(!ResolveCharacterMaskRoi(invalidTiles, kOwners, 65, 33, 303, state).valid);
	invalidTiles = edgeTiles;
	std::swap(invalidTiles.front(), invalidTiles.back());
	CHECK(!ResolveCharacterMaskRoi(invalidTiles, kOwners, 65, 33, 304, state).valid);
	CHECK(!ResolveCharacterMaskRoi(std::span(edgeTiles).first(5), kOwners, 65, 33, 305, state).valid);
	CHECK(!ResolveCharacterMaskRoi({}, kOwners, 0, 33, 306, state).valid);
	CHECK(!ResolveCharacterMaskRoi({}, kOwners, 16385, 33, 307, state).valid);
	CHECK(!ResolveCharacterMaskRoi({}, kOwners, UINT32_MAX, UINT32_MAX, 308, state).valid);
	const std::array<std::uint64_t, 2> duplicateOwner{ 101, 101 };
	const std::array<std::uint64_t, 2> zeroOwner{ 0, 101 };
	CHECK(!ResolveCharacterMaskRoi(tiles, duplicateOwner, kWidth, kHeight, 309, state).valid);
	CHECK(!ResolveCharacterMaskRoi(tiles, zeroOwner, kWidth, kHeight, 310, state).valid);
	CHECK(!state.cacheValid && !state.single.provider.IsValid());
	// A positive mask pixel at the origin must not be mistaken for the all-zero
	// empty sentinel. Positive gaps too small for provider context stay single.
	const std::array origin{ CharacterRect{ 0, 0, 1, 1 } };
	auto originTiles = Tiles(origin);
	const auto originResult = ResolveCharacterMaskRoi(originTiles, kOwners, kWidth, kHeight, 311, state);
	CHECK(originResult.valid && !originResult.empty && originResult.occupiedTiles == 1);
	CHECK(Covered(originResult, originTiles));
	const std::array close{
		CharacterRect{ 400, 100, 450, 200 }, CharacterRect{ 530, 100, 580, 200 }
	};
	auto closeTiles = Tiles(close);
	const auto closeResult = ResolveCharacterMaskRoi(closeTiles, kOwners, kWidth, kHeight, 312, state);
	CHECK(closeResult.computeRegions.count == 0 && Covered(closeResult, closeTiles));
	// Deterministic irregular-mask coverage checks span tiny/partial tile
	// canvases, disconnected components and independently moving support.
	std::uint32_t seed = 0x3758BC1Fu;
	const auto next = [&]() {
		seed = seed * 1664525u + 1013904223u;
		return seed;
	};
	for (std::uint32_t sample = 0; sample < 128; ++sample) {
		const auto width = 1u + next() % kWidth;
		const auto height = 1u + next() % kHeight;
		std::array<CharacterRect, 12> pieces;
		for (auto& rect : pieces) {
			rect.minX = next() % width;
			rect.minY = next() % height;
			rect.maxX = std::min(width, rect.minX + 1u + next() % 80u);
			rect.maxY = std::min(height, rect.minY + 1u + next() % 120u);
		}
		auto generated = Tiles(pieces, width, height);
		const auto generatedResult = ResolveCharacterMaskRoi(generated, kOwners, width, height, 400 + sample, state);
		CHECK(Covered(generatedResult, generated, width, height));
		CHECK(ResolveCharacterMaskRoi(generated, reversedOwners, width, height, 400 + sample, state) == generatedResult);
	}
	// Match all possible authored taps, not just the center pixel, under crops,
	// fractional jitter, scaling and the maximum depth-aware feather radius.
	const std::array sourcePieces{ CharacterRect{ 0, 0, 1, 1 }, CharacterRect{ 16, 12, 17, 13 },
		CharacterRect{ 41, 34, 44, 39 }, CharacterRect{ 66, 48, 67, 49 } };
	const auto sourceTiles = Tiles(sourcePieces, 67, 49);
	for (const auto crop : { ComputeSubrect{ 0, 0, 67, 49 }, ComputeSubrect{ 9, 7, 43, 35 } }) {
		for (const auto radius : { 0u, 1u, 4u }) {
			for (const auto jitter : { -2.75f, -0.45f, 0.0f, 0.45f, 2.75f }) {
				std::vector<CharacterMaskRoiTileBounds> mapped;
				CHECK(MapEarlyCharacterMaskBounds(sourceTiles, 67, 49, crop, 95, 73, jitter, -jitter, radius, mapped));
				const auto sourceSelected = [&](int x, int y) {
					x = crop.baseX + std::clamp(x, 0, static_cast<int>(crop.width) - 1);
					y = crop.baseY + std::clamp(y, 0, static_cast<int>(crop.height) - 1);
					return std::ranges::any_of(sourcePieces, [&](const auto& rect) {
						return x >= static_cast<int>(rect.minX) && x < static_cast<int>(rect.maxX) &&
						       y >= static_cast<int>(rect.minY) && y < static_cast<int>(rect.maxY);
					});
				};
				for (std::uint32_t y = 0; y < 73; ++y) {
					for (std::uint32_t x = 0; x < 95; ++x) {
						const auto sx = (x + 0.5) * crop.width / 95.0 - 0.5 - jitter;
						const auto sy = (y + 0.5) * crop.height / 73.0 - 0.5 + jitter;
						const auto bx = static_cast<int>(std::floor(sx)), by = static_cast<int>(std::floor(sy));
						bool positive = sourceSelected(bx, by) || sourceSelected(bx + 1, by) || sourceSelected(bx, by + 1) || sourceSelected(bx + 1, by + 1);
						for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy)
							for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); ++dx)
								positive = positive || sourceSelected(static_cast<int>(std::floor(sx + 0.5)) + dx, static_cast<int>(std::floor(sy + 0.5)) + dy);
						if (positive) {
							const auto& tile = mapped[(y / 32u) * 3u + x / 32u];
							CHECK(x >= tile.minX && x < tile.maxX && y >= tile.minY && y < tile.maxY);
						}
					}
				}
			}
		}
	}
	std::vector<CharacterMaskRoiTileBounds> mapped;
	CHECK(!MapEarlyCharacterMaskBounds(sourceTiles, 67, 49, { 0, 0, 68, 49 }, 95, 73, 0, 0, 0, mapped));
	CHECK(!MapEarlyCharacterMaskBounds(sourceTiles, 67, 49, { 0, 0, 67, 49 }, 95, 73, 0, 0, 5, mapped));
	CHECK(!MapEarlyCharacterMaskBounds(sourceTiles, 67, 49, { 0, 0, 67, 49 }, 95, 73,
		std::numeric_limits<float>::quiet_NaN(), 0, 0, mapped));
	auto badSource = sourceTiles;
	badSource[0] = { 0, 0, 33, 1 };
	CHECK(!MapEarlyCharacterMaskBounds(badSource, 67, 49, { 0, 0, 67, 49 }, 95, 73, 0, 0, 0, mapped));
	CHECK(mapped.empty());
	const auto separatedTiles = Tiles(std::array{ CharacterRect{ 100, 200, 200, 400 }, CharacterRect{ 1100, 200, 1200, 400 } });
	state = {};
	CHECK(ResolveCharacterMaskRoi(separatedTiles, kOwners, kWidth, kHeight, 1000, state, false, true).computeRegions.count == 2);
	CHECK(ResolveCharacterMaskRoi(separatedTiles, kOwners, kWidth, kHeight, 1000, state, false, false).computeRegions.count == 0);
	CHECK(ResolveCharacterMaskRoi(separatedTiles, kOwners, kWidth, kHeight, 1000, state, false, true).computeRegions.count == 2);
	return 0;
}
