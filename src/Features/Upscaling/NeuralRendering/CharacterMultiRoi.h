#pragma once

#include "CharacterComputeSubrect.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace NeuralRendering
{
	/** Zero means use the established single-region path. No sparse provider ABI is implied. */
	struct CharacterComputeRegionPlan
	{
		std::array<ComputeSubrect, 2> regions{};
		std::array<std::uint64_t, 2> historyKeys{};
		/** Owners-only identity shared by both eyes, independent of visibility epoch. */
		std::array<std::uint64_t, 2> clusterIdentities{};
		std::uint32_t count = 0;

		bool operator==(const CharacterComputeRegionPlan&) const = default;
	};

	struct CharacterMultiRoiActor
	{
		/** Actor lifetime identity, never material draw order or distance rank. */
		std::uint64_t identity = 0;
		CharacterRect rect{};

		bool operator==(const CharacterMultiRoiActor&) const = default;
	};

	enum class CharacterMultiRoiReason : std::uint32_t
	{
		Disabled,
		DiagnosticMode,
		UncertainCoverage,
		TooFewActors,
		ActorCapacity,
		InvalidInput,
		NoDisjointSplit,
		InsufficientSavings,
		EligibilityBridge,
		StableRegionsOverlap,
		Split,
	};

	[[nodiscard]] inline constexpr const char* GetCharacterMultiRoiReasonName(
		CharacterMultiRoiReason a_reason) noexcept
	{
		switch (a_reason) {
		case CharacterMultiRoiReason::Disabled:
			return "disabled";
		case CharacterMultiRoiReason::DiagnosticMode:
			return "diagnostic_mode";
		case CharacterMultiRoiReason::UncertainCoverage:
			return "uncertain_coverage";
		case CharacterMultiRoiReason::TooFewActors:
			return "too_few_actors";
		case CharacterMultiRoiReason::ActorCapacity:
			return "actor_capacity";
		case CharacterMultiRoiReason::InvalidInput:
			return "invalid_input";
		case CharacterMultiRoiReason::NoDisjointSplit:
			return "no_disjoint_split";
		case CharacterMultiRoiReason::InsufficientSavings:
			return "insufficient_area_savings";
		case CharacterMultiRoiReason::EligibilityBridge:
			return "eligibility_bridges_clusters";
		case CharacterMultiRoiReason::StableRegionsOverlap:
			return "stable_regions_overlap";
		case CharacterMultiRoiReason::Split:
			return "split";
		}
		return "invalid_input";
	}

	struct StableCharacterMultiRoi
	{
		struct Cluster
		{
			std::vector<std::uint64_t> owners;
			StableCharacterComputeSubrect stable{};
			std::uint64_t historyKey = 0;
		};
		std::array<Cluster, 2> clusters{};
		/** State before the cached source frame, for a same-frame policy reprepare. */
		std::array<Cluster, 2> beforeFrame{};
		std::vector<CharacterMultiRoiActor> cachedActors;
		std::vector<CharacterRect> cachedEligibility;
		CharacterComputeRegionPlan cachedPlan{};
		CharacterMultiRoiReason cachedReason = CharacterMultiRoiReason::TooFewActors;
		std::uint32_t frame = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		bool cacheValid = false;
	};

	[[nodiscard]] inline constexpr bool CharacterComputeRegionsOverlap(
		const ComputeSubrect& a_left, const ComputeSubrect& a_right) noexcept
	{
		return a_left.IsValid() && a_right.IsValid() &&
		       static_cast<std::uint64_t>(a_left.baseX) < static_cast<std::uint64_t>(a_right.baseX) + a_right.width &&
		       static_cast<std::uint64_t>(a_right.baseX) < static_cast<std::uint64_t>(a_left.baseX) + a_left.width &&
		       static_cast<std::uint64_t>(a_left.baseY) < static_cast<std::uint64_t>(a_right.baseY) + a_right.height &&
		       static_cast<std::uint64_t>(a_right.baseY) < static_cast<std::uint64_t>(a_left.baseY) + a_left.height;
	}

	namespace CharacterMultiRoiDetail
	{
		inline constexpr std::uint64_t kExtraEvaluationPixelReserve = 65536;
		inline constexpr std::size_t kMaximumActors = 128;

		[[nodiscard]] inline std::uint64_t ClusterIdentity(
			std::span<const std::uint64_t> a_owners) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			for (auto owner : a_owners)
				hash = (hash ^ owner) * 1099511628211ull;
			hash = (hash ^ a_owners.size()) * 1099511628211ull;
			return hash ? hash : 1u;
		}

		[[nodiscard]] inline std::uint64_t HistoryKey(
			std::span<const std::uint64_t> a_owners, std::uint32_t a_firstFrame) noexcept
		{
			const auto hash = (ClusterIdentity(a_owners) ^ a_firstFrame) * 1099511628211ull;
			return hash ? hash : 1u;
		}

		[[nodiscard]] inline ComputeSubrect Required(
			const CharacterRect& a_rect, std::uint32_t a_width, std::uint32_t a_height) noexcept
		{
			return BuildCharacterComputeSubrect(std::span(&a_rect, 1), a_width, a_height);
		}

		[[nodiscard]] inline bool CoversEligibility(
			const std::array<ComputeSubrect, 2>& a_regions,
			std::span<const CharacterRect> a_eligibility,
			std::uint32_t a_width, std::uint32_t a_height) noexcept
		{
			for (const auto& rect : a_eligibility) {
				const auto required = Required(rect, a_width, a_height);
				if (!required.IsValid() ||
					(!ContainsComputeSubrect(a_regions[0], required) &&
						!ContainsComputeSubrect(a_regions[1], required)))
					return false;
			}
			return true;
		}

		/** Charges an extra-invocation area reserve; this is not measured GPU break-even. */
		[[nodiscard]] inline bool WorthSplitting(
			const std::array<ComputeSubrect, 2>& a_regions,
			std::uint64_t a_singleArea, bool a_retaining) noexcept
		{
			const auto firstArea = a_regions[0].Area();
			const auto secondArea = a_regions[1].Area();
			if (firstArea > a_singleArea || secondArea > a_singleArea - firstArea)
				return false;
			const auto area = firstArea + secondArea;
			// Pay the extra invocation reserve before requiring 25% net area
			// savings (20% for retention). Subtraction avoids overflow at large sizes.
			const auto saved = a_singleArea - area;
			const auto divisor = a_retaining ? 5u : 4u;
			const auto requiredSaving = a_singleArea / divisor + (a_singleArea % divisor != 0);
			return saved >= kExtraEvaluationPixelReserve &&
			       saved - kExtraEvaluationPixelReserve >= requiredSaving;
		}
	}

	/**
	 * Bounded two-cluster experiment. Every actor and every compacted eligibility
	 * rectangle must remain covered. Disjoint padded/stabilized regions only;
	 * failure falls back to the legacy enclosure without dropping characters.
	 * Persistent ownership is preferred over a marginally better spatial split.
	 * A new ownership episode gets a new key; repeated source frames are idempotent.
	 */
	[[nodiscard]] inline CharacterComputeRegionPlan ResolveCharacterMultiRoi(
		std::span<const CharacterMultiRoiActor> a_actors,
		std::span<const CharacterRect> a_eligibility,
		std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_sourceFrame,
		StableCharacterMultiRoi& a_state, CharacterMultiRoiReason& a_reason)
	{
		using namespace CharacterMultiRoiDetail;
		// Capacity overflow retains ALL actors through the legacy enclosure. This
		// bounds planner allocations/search work independently of material draws.
		if (a_actors.size() > kMaximumActors) {
			a_state = {};
			a_reason = CharacterMultiRoiReason::ActorCapacity;
			return {};
		}
		std::vector<CharacterMultiRoiActor> actors(a_actors.begin(), a_actors.end());
		std::ranges::sort(actors, {}, &CharacterMultiRoiActor::identity);
		if (a_state.width != a_width || a_state.height != a_height)
			a_state = {};
		if (a_state.cacheValid && a_state.frame == a_sourceFrame &&
			a_state.cachedActors == actors &&
			std::ranges::equal(a_state.cachedEligibility, a_eligibility)) {
			a_reason = a_state.cachedReason;
			return a_state.cachedPlan;
		}
		if (a_state.cacheValid && a_state.frame == a_sourceFrame)
			a_state.clusters = a_state.beforeFrame;
		else
			a_state.beforeFrame = a_state.clusters;
		a_state.frame = a_sourceFrame;
		a_state.width = a_width;
		a_state.height = a_height;
		a_state.cachedActors = actors;
		a_state.cachedEligibility.assign(a_eligibility.begin(), a_eligibility.end());
		a_state.cacheValid = true;
		const auto fallback = [&](CharacterMultiRoiReason a_failure) {
			a_state.clusters = {};
			a_state.cachedPlan = {};
			a_state.cachedReason = a_reason = a_failure;
			return CharacterComputeRegionPlan{};
		};
		if (!a_width || !a_height)
			return fallback(CharacterMultiRoiReason::InvalidInput);
		std::uint64_t previousIdentity = 0;
		CharacterRect all{};
		for (const auto& actor : actors) {
			if (!actor.identity || actor.identity == previousIdentity || !actor.rect.IsValid() ||
				actor.rect.maxX > a_width || actor.rect.maxY > a_height)
				return fallback(CharacterMultiRoiReason::InvalidInput);
			previousIdentity = actor.identity;
			all = CharacterRegionPolicy::Union(all, actor.rect);
		}
		if (actors.size() < 2)
			return fallback(CharacterMultiRoiReason::TooFewActors);
		const auto single = BuildCharacterProviderComputeSubrect(Required(all, a_width, a_height), a_width, a_height);
		if (!single.Fits(a_width, a_height))
			return fallback(CharacterMultiRoiReason::InvalidInput);

		struct Candidate
		{
			std::array<CharacterRect, 2> bounds{};
			std::array<std::vector<std::uint64_t>, 2> owners{};
		};
		Candidate selected;
		bool retaining = false;
		// Keep existing cluster membership while it is still valid and useful.
		if (!a_state.clusters[0].owners.empty() && !a_state.clusters[1].owners.empty()) {
			bool allOwned = true;
			for (const auto& actor : actors) {
				std::uint32_t group = 2;
				for (std::uint32_t index = 0; index < 2; ++index) {
					if (std::ranges::binary_search(a_state.clusters[index].owners, actor.identity))
						group = index;
				}
				if (group == 2) {
					allOwned = false;
					break;
				}
				selected.bounds[group] = CharacterRegionPolicy::Union(selected.bounds[group], actor.rect);
				selected.owners[group].push_back(actor.identity);
			}
			retaining = allOwned && selected.owners[0] == a_state.clusters[0].owners &&
			            selected.owners[1] == a_state.clusters[1].owners;
			if (retaining) {
				const std::array padded{
					BuildCharacterProviderComputeSubrect(Required(selected.bounds[0], a_width, a_height), a_width, a_height),
					BuildCharacterProviderComputeSubrect(Required(selected.bounds[1], a_width, a_height), a_width, a_height),
				};
				retaining = !CharacterComputeRegionsOverlap(padded[0], padded[1]) &&
				            WorthSplitting(padded, single.Area(), true) &&
				            CoversEligibility(padded, a_eligibility, a_width, a_height);
			}
		}
		if (!retaining) {
			std::uint64_t bestArea = UINT64_MAX;
			std::uint32_t bestAxis = 0;
			std::size_t bestCut = 0;
			CharacterMultiRoiReason failure = CharacterMultiRoiReason::NoDisjointSplit;
			const auto spatialOrder = [](std::uint32_t axis, const auto& left, const auto& right) {
				const auto center = [axis](const auto& actor) {
					return axis == 0 ? static_cast<std::uint64_t>(actor.rect.minX) + actor.rect.maxX :
					                   static_cast<std::uint64_t>(actor.rect.minY) + actor.rect.maxY;
				};
				return center(left) != center(right) ? center(left) < center(right) : left.identity < right.identity;
			};
			for (std::uint32_t axis = 0; axis < 2; ++axis) {
				auto ordered = actors;
				std::ranges::sort(ordered, [&](const auto& left, const auto& right) {
					return spatialOrder(axis, left, right);
				});
				std::vector<CharacterRect> suffix(ordered.size());
				for (std::size_t index = ordered.size(); index-- > 0;)
					suffix[index] = CharacterRegionPolicy::Union(ordered[index].rect,
						index + 1 < ordered.size() ? suffix[index + 1] : CharacterRect{});
				CharacterRect prefix{};
				for (std::size_t cut = 1; cut < ordered.size(); ++cut) {
					prefix = CharacterRegionPolicy::Union(prefix, ordered[cut - 1].rect);
					const std::array padded{
						BuildCharacterProviderComputeSubrect(Required(prefix, a_width, a_height), a_width, a_height),
						BuildCharacterProviderComputeSubrect(Required(suffix[cut], a_width, a_height), a_width, a_height),
					};
					if (CharacterComputeRegionsOverlap(padded[0], padded[1]))
						continue;
					if (!WorthSplitting(padded, single.Area(), false)) {
						failure = CharacterMultiRoiReason::InsufficientSavings;
						continue;
					}
					if (!CoversEligibility(padded, a_eligibility, a_width, a_height)) {
						failure = CharacterMultiRoiReason::EligibilityBridge;
						continue;
					}
					const auto area = padded[0].Area() + padded[1].Area();
					if (area >= bestArea)
						continue;
					bestArea = area;
					bestAxis = axis;
					bestCut = cut;
					selected = {};
					selected.bounds = { prefix, suffix[cut] };
				}
			}
			if (bestArea == UINT64_MAX)
				return fallback(failure);
			auto ordered = actors;
			std::ranges::sort(ordered, [&](const auto& left, const auto& right) {
				return spatialOrder(bestAxis, left, right);
			});
			for (std::size_t index = 0; index < ordered.size(); ++index)
				selected.owners[index < bestCut ? 0 : 1].push_back(ordered[index].identity);
			for (auto& owners : selected.owners)
				std::ranges::sort(owners);
			// Match surviving ownership first; otherwise use a deterministic actor key.
			const bool reversedMatch = selected.owners[1] == a_state.clusters[0].owners ||
			                           selected.owners[0] == a_state.clusters[1].owners;
			const bool directMatch = selected.owners[0] == a_state.clusters[0].owners ||
			                         selected.owners[1] == a_state.clusters[1].owners;
			if (reversedMatch || (!directMatch && selected.owners[1] < selected.owners[0])) {
				std::swap(selected.owners[0], selected.owners[1]);
				std::swap(selected.bounds[0], selected.bounds[1]);
			}
		}

		auto next = a_state.clusters;
		CharacterComputeRegionPlan result;
		for (std::uint32_t index = 0; index < 2; ++index) {
			if (next[index].owners != selected.owners[index]) {
				next[index] = {};
				next[index].owners = selected.owners[index];
				next[index].historyKey = HistoryKey(next[index].owners, a_sourceFrame);
			}
			result.regions[index] = ResolveStableCharacterComputeSubrect(
				Required(selected.bounds[index], a_width, a_height), a_width, a_height, next[index].stable);
			result.historyKeys[index] = next[index].historyKey;
			result.clusterIdentities[index] = ClusterIdentity(next[index].owners);
			if (!result.regions[index].Fits(a_width, a_height))
				return fallback(CharacterMultiRoiReason::InvalidInput);
		}
		if (CharacterComputeRegionsOverlap(result.regions[0], result.regions[1]))
			return fallback(CharacterMultiRoiReason::StableRegionsOverlap);
		if (!WorthSplitting(result.regions, single.Area(), retaining))
			return fallback(CharacterMultiRoiReason::InsufficientSavings);
		if (!CoversEligibility(result.regions, a_eligibility, a_width, a_height))
			return fallback(CharacterMultiRoiReason::EligibilityBridge);
		if (result.historyKeys[0] == result.historyKeys[1] ||
			result.clusterIdentities[0] == result.clusterIdentities[1])
			return fallback(CharacterMultiRoiReason::InvalidInput);
		result.count = 2;
		a_state.clusters = std::move(next);
		a_state.cachedReason = a_reason = CharacterMultiRoiReason::Split;
		a_state.cachedPlan = result;
		return result;
	}
}
