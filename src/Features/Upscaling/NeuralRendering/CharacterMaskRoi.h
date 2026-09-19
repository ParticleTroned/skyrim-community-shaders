#pragma once

#include "CharacterMultiRoi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterMaskRoiTileSize = 32;
	inline constexpr std::uint32_t kCharacterMaskRoiMaximumExtent = 16384;

	/** GPU reduction ABI: exact nonzero-pixel enclosure in each row-major tile. */
	struct CharacterMaskRoiTileBounds
	{
		std::uint32_t minX = 0;
		std::uint32_t minY = 0;
		std::uint32_t maxX = 0;
		std::uint32_t maxY = 0;
		bool operator==(const CharacterMaskRoiTileBounds&) const = default;
	};
	static_assert(sizeof(CharacterMaskRoiTileBounds) == 16);

	struct CharacterMaskRoiResult
	{
		/** False means discard this readback and retain conservative CPU coverage. */
		bool valid = false;
		bool empty = false;
		ComputeSubrect requiredSubrect{};
		ComputeSubrect computeSubrect{};
		CharacterComputeRegionPlan computeRegions{};
		CharacterMultiRoiReason multiRoiReason = CharacterMultiRoiReason::InvalidInput;
		CharacterMultiRoiDiagnostics diagnostics{};
		std::uint32_t occupiedTiles = 0;
		/** 0 = X, 1 = Y; only meaningful for a two-region result. */
		std::uint32_t splitAxis = 0;
		std::uint32_t splitTile = 0;
		bool operator==(const CharacterMaskRoiResult&) const = default;
	};

	struct StableCharacterMaskRoi
	{
		StableCharacterComputeSubrect single{};
		StableCharacterMultiRoi multi{};
		std::vector<CharacterMaskRoiTileBounds> cachedTiles;
		std::vector<std::uint64_t> cachedOwners;
		CharacterMaskRoiResult cachedResult{};
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t frame = 0;
		bool cacheValid = false;
		bool cachedSavingsGate = true;
		bool cachedAllowSplit = true;
	};

	namespace CharacterMaskRoiDetail
	{
		[[nodiscard]] inline std::array<std::uint64_t, 2> SpatialIdentities(
			std::span<const std::uint64_t> a_sortedOwners, std::uint32_t a_axis) noexcept
		{
			// The mask has no actor ID. These identify spatial clusters of a stable
			// selected-actor population, not an assertion of pixel-level ownership.
			auto hash = CharacterMultiRoiDetail::ClusterIdentity(a_sortedOwners);
			hash = (hash ^ (0x4D41534B524F4900ull + a_axis)) * 1099511628211ull;
			std::array<std::uint64_t, 2> result{
				(hash ^ 1u) * 1099511628211ull,
				(hash ^ 2u) * 1099511628211ull,
			};
			// Multiplication by an odd number is injective modulo 2^64.
			// Replace the at-most-one zero with a third, distinct image.
			for (auto& value : result)
				if (!value)
					value = (hash ^ 3u) * 1099511628211ull;
			return result;
		}

		struct Candidate
		{
			std::array<CharacterRect, 2> bounds{};
			std::uint32_t axis = 0;
			std::uint32_t cut = 0;
			std::uint64_t area = UINT64_MAX;
		};
	}

	/**
	 * Accept only a fresh, completed reduction of the final quantized selection
	 * mask or a proven current-source superset of its complete sampling support. The caller owns freshness/synchronization and must never call this
	 * with stale tiles to justify excluding current-frame pixels. Timeouts should
	 * retain this state but use the CPU fallback, not cachedResult's rectangles.
	 * CPU eligibility rectangles are intentionally absent: they are conservative
	 * mask-authoring limits, not the exact support that inference must cover.
	 * An optional shared single-region envelope lets CPU fallback and GPU bounds
	 * retain one history. Only valid nonempty results commit that shared state;
	 * the caller owns its crop/generation resets and must not pre-age it here.
	 */
	[[nodiscard]] inline CharacterMaskRoiResult ResolveCharacterMaskRoi(
		std::span<const CharacterMaskRoiTileBounds> a_tiles,
		std::span<const std::uint64_t> a_actorLifetimeIds,
		std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_sourceFrame,
		StableCharacterMaskRoi& a_state, bool a_savingsGate = true, bool a_allowSplit = true,
		StableCharacterComputeSubrect* a_sharedSingle = nullptr)
	{
		using namespace CharacterMaskRoiDetail;
		const auto invalid = [&]() {
			a_state = {};
			return CharacterMaskRoiResult{};
		};
		if (!a_width || !a_height || a_width > kCharacterMaskRoiMaximumExtent ||
			a_height > kCharacterMaskRoiMaximumExtent ||
			a_actorLifetimeIds.size() > CharacterMultiRoiDetail::kMaximumActors)
			return invalid();
		const auto columns = (a_width + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
		const auto rows = (a_height + kCharacterMaskRoiTileSize - 1u) / kCharacterMaskRoiTileSize;
		if (a_tiles.size() != static_cast<std::size_t>(columns) * rows)
			return invalid();
		std::vector<std::uint64_t> owners(a_actorLifetimeIds.begin(), a_actorLifetimeIds.end());
		std::ranges::sort(owners);
		if ((!owners.empty() && !owners.front()) ||
			std::adjacent_find(owners.begin(), owners.end()) != owners.end())
			return invalid();
		if (a_state.width != a_width || a_state.height != a_height)
			a_state = {};
		const bool sameSourceFrame = a_state.cacheValid && a_state.frame == a_sourceFrame;
		const bool sameInputs = sameSourceFrame &&
		                        a_state.cachedSavingsGate == a_savingsGate && a_state.cachedAllowSplit == a_allowSplit &&
		                        a_state.cachedOwners == owners && std::ranges::equal(a_state.cachedTiles, a_tiles);
		if (sameInputs &&
			(!a_sharedSingle || a_state.cachedResult.empty ||
				(a_sharedSingle->width == a_width && a_sharedSingle->height == a_height &&
					a_state.cachedResult.diagnostics.singleRegion == a_sharedSingle->provider)))
			return a_state.cachedResult;
		auto single = a_sharedSingle ? *a_sharedSingle : a_state.single;
		// Changed ownership/policy starts a new episode; readback availability
		// alone must retain the shared conservative envelope.
		if (a_state.cacheValid && ((sameSourceFrame && !sameInputs) || a_state.cachedOwners != owners)) {
			single = {};
			a_state.multi = {};
		}

		std::array<std::vector<CharacterRect>, 2> stripes{
			std::vector<CharacterRect>(columns), std::vector<CharacterRect>(rows)
		};
		std::vector<CharacterRect> occupied;
		CharacterRect all{};
		for (std::size_t index = 0; index < a_tiles.size(); ++index) {
			const auto& tile = a_tiles[index];
			if (tile == CharacterMaskRoiTileBounds{})
				continue;
			const CharacterRect rect{ tile.minX, tile.minY, tile.maxX, tile.maxY };
			const auto column = static_cast<std::uint32_t>(index % columns);
			const auto row = static_cast<std::uint32_t>(index / columns);
			const auto left = column * kCharacterMaskRoiTileSize;
			const auto top = row * kCharacterMaskRoiTileSize;
			if (!rect.IsValid() || rect.minX < left || rect.minY < top ||
				rect.maxX > std::min(left + kCharacterMaskRoiTileSize, a_width) ||
				rect.maxY > std::min(top + kCharacterMaskRoiTileSize, a_height))
				return invalid();
			all = CharacterRegionPolicy::Union(all, rect);
			stripes[0][column] = CharacterRegionPolicy::Union(stripes[0][column], rect);
			stripes[1][row] = CharacterRegionPolicy::Union(stripes[1][row], rect);
			occupied.push_back(rect);
		}
		CharacterMaskRoiResult result;
		result.diagnostics.savingsGateEnabled = a_savingsGate;
		result.valid = true;
		result.empty = occupied.empty();
		result.occupiedTiles = static_cast<std::uint32_t>(occupied.size());
		result.multiRoiReason = CharacterMultiRoiReason::TooFewActors;
		if (result.empty) {
			single = {};
			a_state.multi = {};
		} else {
			result.requiredSubrect = BuildCharacterComputeSubrect(std::span(&all, 1), a_width, a_height);
			result.computeSubrect = ResolveStableCharacterComputeSubrect(
				result.requiredSubrect, a_width, a_height, single);
			if (!result.computeSubrect.Fits(a_width, a_height))
				return invalid();
			result.diagnostics.singleRegion = result.computeSubrect;
			if (a_allowSplit && owners.size() >= 2) {
				const auto singleFallback = result.computeSubrect;
				Candidate best;
				Candidate retained;
				result.multiRoiReason = CharacterMultiRoiReason::NoDisjointSplit;
				for (std::uint32_t axis = 0; axis < 2; ++axis) {
					const auto& line = stripes[axis];
					std::vector<CharacterRect> suffix(line.size());
					for (std::size_t index = line.size(); index-- > 0;)
						suffix[index] = CharacterRegionPolicy::Union(line[index],
							index + 1u < line.size() ? suffix[index + 1u] : CharacterRect{});
					CharacterRect prefix{};
					for (std::uint32_t cut = 1; cut < line.size(); ++cut) {
						prefix = CharacterRegionPolicy::Union(prefix, line[cut - 1u]);
						const auto& right = suffix[cut];
						if (!prefix.IsValid() || !right.IsValid())
							continue;
						// Never cut through a positive-mask tile enclosure. The strict
						// spatial gap is current-frame proof, even with broad CPU bounds.
						if ((axis == 0 ? prefix.maxX >= right.minX : prefix.maxY >= right.minY))
							continue;
						const std::array padded{
							CharacterMultiRoiDetail::Required(prefix, a_width, a_height),
							CharacterMultiRoiDetail::Required(right, a_width, a_height),
						};
						const std::array provider{
							BuildCharacterProviderComputeSubrect(padded[0], a_width, a_height),
							BuildCharacterProviderComputeSubrect(padded[1], a_width, a_height),
						};
						const bool retaining = a_state.cachedResult.computeRegions.count == 2 &&
						                       a_state.cachedResult.splitAxis == axis && a_state.cachedResult.splitTile == cut &&
						                       a_state.cachedOwners == owners;
						const bool overlaps = CharacterComputeRegionsOverlap(provider[0], provider[1]);
						const auto cost = CharacterMultiRoiDetail::CalculateSplitCost(provider, singleFallback.Area(), retaining);
						auto& diagnostics = result.diagnostics;
						++diagnostics.candidatesConsidered;
						diagnostics.overlappingCandidates += overlaps;
						const bool worthwhile = CharacterMultiRoiDetail::WorthSplitting(provider, singleFallback.Area(), retaining, a_savingsGate);
						diagnostics.savingsRejectedCandidates += !overlaps && !worthwhile;
						if (!diagnostics.candidateAvailable || (!overlaps && diagnostics.candidateOverlaps) ||
							(overlaps == diagnostics.candidateOverlaps && cost.valid && cost.splitPixels < diagnostics.cost.splitPixels)) {
							diagnostics.candidateAvailable = true;
							diagnostics.candidateRegions = provider;
							diagnostics.candidateOverlaps = overlaps;
							diagnostics.candidateCoversEligibility = true;
							diagnostics.retaining = retaining;
							diagnostics.cost = cost;
						}
						if (overlaps)
							continue;
						if (!worthwhile) {
							result.multiRoiReason = CharacterMultiRoiReason::InsufficientSavings;
							continue;
						}
						Candidate candidate{ { prefix, right }, axis, cut, provider[0].Area() + provider[1].Area() };
						if (retaining)
							retained = candidate;
						if (candidate.area < best.area)
							best = candidate;
					}
				}
				if (retained.area != UINT64_MAX)
					best = retained;
				if (best.area != UINT64_MAX) {
					const auto identities = SpatialIdentities(owners, best.axis);
					const std::array actors{
						CharacterMultiRoiActor{ identities[0], best.bounds[0] },
						CharacterMultiRoiActor{ identities[1], best.bounds[1] },
					};
					result.computeRegions = ResolveCharacterMultiRoi(actors, occupied,
						a_width, a_height, a_sourceFrame, a_state.multi, result.multiRoiReason,
						a_savingsGate, singleFallback);
					result.diagnostics = a_state.multi.diagnostics;
					if (result.computeRegions.count == 2) {
						result.computeSubrect = UnionCharacterComputeSubrect(
							result.computeRegions.regions[0], result.computeRegions.regions[1]);
						result.splitAxis = best.axis;
						result.splitTile = best.cut;
					}
				} else {
					a_state.multi = {};
				}
			} else {
				a_state.multi = {};
			}
		}
		a_state.cachedTiles.assign(a_tiles.begin(), a_tiles.end());
		a_state.cachedSavingsGate = a_savingsGate;
		a_state.cachedAllowSplit = a_allowSplit;
		a_state.cachedOwners = std::move(owners);
		a_state.cachedResult = result;
		a_state.width = a_width;
		a_state.height = a_height;
		a_state.frame = a_sourceFrame;
		a_state.cacheValid = true;
		a_state.single = single;
		if (a_sharedSingle && !result.empty)
			*a_sharedSingle = single;
		return result;
	}
}
