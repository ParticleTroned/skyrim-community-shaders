#pragma once

#include "CharacterComputeSubrect.h"
#include "RegionCapacity.h"
#include "RoiDescriptor.h"

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
		std::array<ComputeSubrect, kEnabledRegionsPerEye> regions{};
		std::array<std::uint64_t, kEnabledRegionsPerEye> historyKeys{};
		/** Actor lifetime or eye-local spatial identity; rank never establishes correspondence. */
		std::array<std::uint64_t, kEnabledRegionsPerEye> clusterIdentities{};
		/** Dense descriptors retain independent physical history banks through local removal. */
		std::array<std::uint32_t, kEnabledRegionsPerEye> regionSlots = [] {
			std::array<std::uint32_t, kEnabledRegionsPerEye> slots{};
			for (std::uint32_t i = 0; i < slots.size(); ++i)
				slots[i] = i;
			return slots;
		}();
		std::uint32_t count = 0;
		bool spatialTracks = false;
		std::array<bool, kEnabledRegionsPerEye> identityConfident{};
		/** Same source and ordering as regions; count == 0 still selects the legacy single. */
		std::array<RoiDescriptor, kEnabledRegionsPerEye> roi{};

		bool operator==(const CharacterComputeRegionPlan&) const = default;
	};

	struct CharacterMultiRoiActor
	{
		/** Actor lifetime identity, never material draw order or distance rank. */
		std::uint64_t identity = 0;
		CharacterRect rect{};

		bool operator==(const CharacterMultiRoiActor&) const = default;
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Actual private-output writes, distinct from the mask's original planning envelope. */
	struct CharacterOutputPlan
	{
		ComputeSubrect enclosure{};
		CharacterComputeRegionPlan regions{};
	};
#endif

	/** Area accounting uses padded provider rectangles, never semantic mask occupancy. */
	struct CharacterMultiRoiCost
	{
		std::uint64_t singlePixels = 0;
		std::uint64_t splitPixels = 0;
		std::uint64_t savedPixels = 0;
		std::uint64_t additionalPixels = 0;
		std::uint64_t requiredRelativePixels = 0;
		bool valid = false;
		bool positiveSavings = false;
		bool meetsHeuristic = false;
		std::uint64_t extraEvaluationPixelReserve = 0;
		bool operator==(const CharacterMultiRoiCost&) const = default;
	};

	/** Current-source candidate evidence survives a single-region fallback. */
	struct CharacterMultiRoiDiagnostics
	{
		ComputeSubrect singleRegion{};
		std::array<ComputeSubrect, kEnabledRegionsPerEye> candidateRegions{};
		CharacterMultiRoiCost cost{};
		std::uint32_t candidateCount = 0;
		std::uint32_t candidatesConsidered = 0;
		std::uint32_t overlappingCandidates = 0;
		std::uint32_t coverageRejectedCandidates = 0;
		std::uint32_t savingsRejectedCandidates = 0;
		bool savingsGateEnabled = true;
		bool candidateAvailable = false;
		bool candidateOverlaps = false;
		bool candidateCoversEligibility = false;
		bool stabilizedCandidate = false;
		bool retaining = false;
		bool operator==(const CharacterMultiRoiDiagnostics&) const = default;
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
		std::array<Cluster, kEnabledRegionsPerEye> clusters{};
		/** State before the cached source frame, for a same-frame policy reprepare. */
		std::array<Cluster, kEnabledRegionsPerEye> beforeFrame{};
		std::vector<CharacterMultiRoiActor> cachedActors;
		std::vector<CharacterRect> cachedEligibility;
		CharacterComputeRegionPlan cachedPlan{};
		ComputeSubrect cachedSingleRegion{};
		bool cachedSavingsGate = true;
		std::uint32_t cachedRegionLimit = kDefaultRegionsPerEye;
		CharacterMultiRoiDiagnostics diagnostics{};
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

	/** Zero rejects invalid output coverage before any copy or composition dispatch. */
	[[nodiscard]] inline constexpr std::uint64_t CharacterRegionOutputPixels(
		const CharacterComputeRegionPlan& plan, const ComputeSubrect& enclosure,
		std::uint32_t width, std::uint32_t height) noexcept
	{
		if (plan.count > kEnabledRegionsPerEye || !enclosure.Fits(width, height))
			return 0;
		if (plan.count == 0)
			return enclosure.Area();
		std::uint64_t pixels = 0;
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			const auto& region = plan.regions[i];
			if (!region.Fits(width, height) || !ContainsComputeSubrect(enclosure, region))
				return 0;
			for (std::uint32_t j = 0; j < i; ++j)
				if (CharacterComputeRegionsOverlap(region, plan.regions[j]))
					return 0;
			pixels += region.Area();
		}
		return pixels;
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
			std::span<const ComputeSubrect> a_regions,
			std::span<const CharacterRect> a_eligibility,
			std::uint32_t a_width, std::uint32_t a_height) noexcept
		{
			for (const auto& rect : a_eligibility) {
				const auto required = Required(rect, a_width, a_height);
				if (!required.IsValid() ||
					!std::ranges::any_of(a_regions, [&](const auto& region) { return ContainsComputeSubrect(region, required); }))
					return false;
			}
			return true;
		}

		/** Exact integer accounting; the reserve is a heuristic, not measured GPU cost. */
		[[nodiscard]] inline CharacterMultiRoiCost CalculateSplitCost(
			std::span<const ComputeSubrect> a_regions,
			std::uint64_t a_singleArea, bool a_retaining) noexcept
		{
			CharacterMultiRoiCost cost{};
			cost.singlePixels = a_singleArea;
			const auto divisor = a_retaining ? 5u : 4u;
			cost.requiredRelativePixels = a_singleArea / divisor + (a_singleArea % divisor != 0);
			if (!a_singleArea || a_regions.size() < 2 || a_regions.size() > kEnabledRegionsPerEye)
				return cost;
			for (const auto& region : a_regions) {
				const auto area = region.Area();
				if (!area || area > UINT64_MAX - cost.splitPixels)
					return cost;
				cost.splitPixels += area;
			}
			cost.valid = true;
			cost.extraEvaluationPixelReserve = kExtraEvaluationPixelReserve * (a_regions.size() - 1);
			cost.positiveSavings = cost.splitPixels < a_singleArea;
			cost.savedPixels = cost.positiveSavings ? a_singleArea - cost.splitPixels : 0;
			cost.additionalPixels = cost.splitPixels > a_singleArea ? cost.splitPixels - a_singleArea : 0;
			cost.meetsHeuristic = cost.savedPixels >= cost.extraEvaluationPixelReserve &&
			                      cost.savedPixels - cost.extraEvaluationPixelReserve >= cost.requiredRelativePixels;
			return cost;
		}

		/** Disabling the heuristic still requires a strictly smaller total pixel area. */
		[[nodiscard]] inline bool WorthSplitting(
			std::span<const ComputeSubrect> a_regions,
			std::uint64_t a_singleArea, bool a_retaining, bool a_savingsGate = true) noexcept
		{
			const auto cost = CalculateSplitCost(a_regions, a_singleArea, a_retaining);
			return cost.valid && cost.positiveSavings && (!a_savingsGate || cost.meetsHeuristic);
		}
		struct PartitionCandidate
		{
			std::vector<std::vector<std::size_t>> groups;
			std::vector<CharacterRect> bounds;
			std::vector<ComputeSubrect> providers;
			std::uint64_t area = 0;
		};
		inline void PreparePartition(PartitionCandidate& candidate, std::span<const CharacterMultiRoiActor> actors, std::uint32_t a_width, std::uint32_t a_height)
		{
			for (auto& group : candidate.groups)
				std::ranges::sort(group);
			std::ranges::sort(candidate.groups);
			candidate.bounds.assign(candidate.groups.size(), {});
			candidate.providers.clear();
			candidate.area = 0;
			for (std::size_t i = 0; i < candidate.groups.size(); ++i) {
				for (auto index : candidate.groups[i])
					candidate.bounds[i] = CharacterRegionPolicy::Union(candidate.bounds[i], actors[index].rect);
				const auto provider = BuildCharacterProviderComputeSubrect(Required(candidate.bounds[i], a_width, a_height), a_width, a_height);
				candidate.providers.push_back(provider);
				candidate.area = provider.Area() > UINT64_MAX - candidate.area ? UINT64_MAX : candidate.area + provider.Area();
			}
		}

		inline constexpr std::size_t kPartitionBeamWidth = 16;
		inline constexpr std::uint32_t kPartitionCandidateBudget = 16384;

		/** Visit complete cuts while retaining intermediate cuts that later splits may repair. */
		template <class Visit>
		inline std::uint32_t VisitPartitions(std::span<const CharacterMultiRoiActor> actors,
			std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_regionLimit, Visit visit, std::uint32_t budget = kPartitionCandidateBudget)
		{
			if (actors.empty() || actors.size() > kMaximumActors || !a_width || !a_height || a_regionLimit > kEnabledRegionsPerEye)
				return 0;
			PartitionCandidate initial;
			initial.groups.emplace_back();
			for (std::size_t i = 0; i < actors.size(); ++i)
				initial.groups.front().push_back(i);
			std::vector<PartitionCandidate> beam{ std::move(initial) };
			std::uint32_t considered = 0;
			for (std::uint32_t count = 2; count <= a_regionLimit && !beam.empty() && considered < budget; ++count) {
				std::vector<PartitionCandidate> next;
				for (const auto& parent : beam)
					for (std::size_t group = 0; group < parent.groups.size(); ++group)
						for (std::uint32_t axis = 0; axis < 2; ++axis) {
							auto ordered = parent.groups[group];
							std::ranges::sort(ordered, [&](auto left, auto right) {
								const auto center = [&](auto i) { const auto& r = actors[i].rect; return axis == 0 ? std::uint64_t(r.minX) + r.maxX : std::uint64_t(r.minY) + r.maxY; };
								return center(left) != center(right) ? center(left) < center(right) : left < right;
							});
							for (std::size_t cut = 1; cut < ordered.size() && considered < budget; ++cut) {
								++considered;
								PartitionCandidate candidate;
								candidate.groups = parent.groups;
								candidate.groups[group].assign(ordered.begin(), ordered.begin() + cut);
								candidate.groups.emplace_back(ordered.begin() + cut, ordered.end());
								PreparePartition(candidate, actors, a_width, a_height);
								visit(candidate);
								if (count == a_regionLimit)
									continue;
								// Keep intermediate cuts even when their savings/overlap need a later local split.
								if (std::ranges::any_of(next, [&](const auto& old) { return old.groups == candidate.groups; }))
									continue;
								next.push_back(std::move(candidate));
								std::ranges::stable_sort(next, {}, &PartitionCandidate::area);
								if (next.size() > kPartitionBeamWidth)
									next.pop_back();
							}
						}
				beam = std::move(next);
			}
			return considered;
		}

		template <class Record, class Admit>
		[[nodiscard]] inline PartitionCandidate FindPartition(std::span<const CharacterMultiRoiActor> actors,
			std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_regionLimit, Record recordCandidate, Admit admissible)
		{
			PartitionCandidate selected;
			VisitPartitions(actors, a_width, a_height, a_regionLimit, [&](const PartitionCandidate& candidate) {
				recordCandidate(candidate.providers, false, false);
				if ((selected.groups.empty() || candidate.area < selected.area) && admissible(candidate, false))
					selected = candidate;
			});
			return selected;
		}

		/** Repair colliding history envelopes while retaining unaffected banks. */
		[[nodiscard]] inline bool RepairOverlappingHistories(CharacterComputeRegionPlan& result,
			std::array<StableCharacterMultiRoi::Cluster, kEnabledRegionsPerEye>& next,
			std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_sourceFrame)
		{
			bool mergedOverlap = false;
			for (bool changed = true; changed && result.count > 1;) {
				changed = false;
				for (std::uint32_t i = 0; i < result.count && !changed; ++i)
					for (std::uint32_t j = i + 1; j < result.count; ++j) {
						if (!CharacterComputeRegionsOverlap(result.regions[i], result.regions[j]))
							continue;
						auto& retained = next[result.regionSlots[i]];
						auto& removed = next[result.regionSlots[j]];
						retained.owners.insert(retained.owners.end(), removed.owners.begin(), removed.owners.end());
						std::ranges::sort(retained.owners);
						retained.historyKey = HistoryKey(retained.owners, a_sourceFrame);
						retained.stable = {};
						retained.stable.width = a_width;
						retained.stable.height = a_height;
						retained.stable.provider = UnionCharacterComputeSubrect(result.regions[i], result.regions[j]);
						result.regions[i] = retained.stable.provider;
						result.historyKeys[i] = retained.historyKey;
						result.clusterIdentities[i] = ClusterIdentity(retained.owners);
						removed = {};
						for (std::uint32_t k = j + 1; k < result.count; ++k) {
							result.regions[k - 1] = result.regions[k];
							result.regionSlots[k - 1] = result.regionSlots[k];
							result.historyKeys[k - 1] = result.historyKeys[k];
							result.clusterIdentities[k - 1] = result.clusterIdentities[k];
						}
						--result.count;
						result.regions[result.count] = {};
						result.historyKeys[result.count] = 0;
						result.clusterIdentities[result.count] = 0;
						changed = mergedOverlap = true;
						break;
					}
			}
			return mergedOverlap;
		}

	}

	/** Sum only explicit regions; zero retains the distinct legacy-single convention. */
	[[nodiscard]] inline std::uint64_t CharacterRegionPixels(const CharacterComputeRegionPlan& plan) noexcept
	{
		std::uint64_t result = 0;
		for (std::uint32_t i = 0; i < std::min(plan.count, kEnabledRegionsPerEye); ++i)
			result = plan.regions[i].Area() > UINT64_MAX - result ? UINT64_MAX : result + plan.regions[i].Area();
		return result;
	}

	[[nodiscard]] inline ComputeSubrect CharacterRegionEnclosure(const CharacterComputeRegionPlan& plan) noexcept
	{
		ComputeSubrect result{};
		for (std::uint32_t i = 0; i < std::min(plan.count, kEnabledRegionsPerEye); ++i)
			result = UnionCharacterComputeSubrect(result, plan.regions[i]);
		return result;
	}

	/** Bounded cut search retains complete coverage and independent ownership episodes. */
	[[nodiscard]] inline CharacterComputeRegionPlan ResolveCharacterMultiRoi(
		std::span<const CharacterMultiRoiActor> a_actors,
		std::span<const CharacterRect> a_eligibility,
		std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_sourceFrame,
		StableCharacterMultiRoi& a_state, CharacterMultiRoiReason& a_reason,
		bool a_savingsGate = true, ComputeSubrect a_singleRegion = {},
		std::uint32_t a_regionLimit = kDefaultRegionsPerEye)
	{
		using namespace CharacterMultiRoiDetail;
		if (a_actors.size() > kMaximumActors || a_regionLimit < 1 || a_regionLimit > kEnabledRegionsPerEye) {
			a_state = {};
			a_reason = a_actors.size() > kMaximumActors ? CharacterMultiRoiReason::ActorCapacity : CharacterMultiRoiReason::InvalidInput;
			return {};
		}
		std::vector<CharacterMultiRoiActor> actors(a_actors.begin(), a_actors.end());
		std::ranges::sort(actors, {}, &CharacterMultiRoiActor::identity);
		if (a_state.width != a_width || a_state.height != a_height)
			a_state = {};
		if (a_state.cacheValid && a_state.frame == a_sourceFrame &&
			a_state.cachedSingleRegion == a_singleRegion && a_state.cachedSavingsGate == a_savingsGate &&
			a_state.cachedRegionLimit == a_regionLimit && a_state.cachedActors == actors &&
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
		a_state.cachedSingleRegion = a_singleRegion;
		a_state.cachedSavingsGate = a_savingsGate;
		const bool sameLimit = a_state.cachedRegionLimit == a_regionLimit;
		a_state.cachedRegionLimit = a_regionLimit;
		a_state.diagnostics = {};
		auto& diagnostics = a_state.diagnostics;
		diagnostics.savingsGateEnabled = a_savingsGate;
		a_state.cachedEligibility.assign(a_eligibility.begin(), a_eligibility.end());
		a_state.cacheValid = true;
		const auto fallback = [&](CharacterMultiRoiReason failure) {
			a_state.clusters = {};
			a_state.cachedPlan = {};
			a_state.cachedReason = a_reason = failure;
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
		if (actors.size() < 2 || a_regionLimit < 2)
			return fallback(CharacterMultiRoiReason::TooFewActors);
		const auto required = BuildCharacterComputeSubrect(a_eligibility, a_width, a_height);
		const auto single = a_singleRegion == ComputeSubrect{} ?
		                        BuildCharacterProviderComputeSubrect(required, a_width, a_height) :
		                        a_singleRegion;
		if (!single.Fits(a_width, a_height) || !ContainsComputeSubrect(single, required) ||
			!ContainsComputeSubrect(single, Required(all, a_width, a_height)))
			return fallback(CharacterMultiRoiReason::InvalidInput);
		diagnostics.singleRegion = single;
		int bestDiagnosticRank = -1;
		const auto overlaps = [](std::span<const ComputeSubrect> regions) {
			for (std::size_t i = 0; i < regions.size(); ++i)
				for (std::size_t j = i + 1; j < regions.size(); ++j)
					if (CharacterComputeRegionsOverlap(regions[i], regions[j]))
						return true;
			return false;
		};
		const auto recordCandidate = [&](std::span<const ComputeSubrect> regions, bool retaining, bool stabilized) {
			const bool overlap = overlaps(regions);
			const bool covers = CoversEligibility(regions, a_eligibility, a_width, a_height);
			const auto cost = CalculateSplitCost(regions, single.Area(), retaining);
			const auto rank = overlap ? 0 : covers ? 2 :
			                                         1;
			++diagnostics.candidatesConsidered;
			diagnostics.overlappingCandidates += overlap;
			diagnostics.coverageRejectedCandidates += !overlap && !covers;
			diagnostics.savingsRejectedCandidates += !overlap && covers && !WorthSplitting(regions, single.Area(), retaining, a_savingsGate);
			if (!stabilized && (rank < bestDiagnosticRank ||
								   (rank == bestDiagnosticRank && diagnostics.cost.valid && (!cost.valid || cost.splitPixels >= diagnostics.cost.splitPixels))))
				return;
			bestDiagnosticRank = rank;
			diagnostics.candidateAvailable = true;
			diagnostics.candidateRegions = {};
			std::ranges::copy(regions, diagnostics.candidateRegions.begin());
			diagnostics.candidateCount = static_cast<std::uint32_t>(regions.size());
			diagnostics.candidateOverlaps = overlap;
			diagnostics.candidateCoversEligibility = covers;
			diagnostics.stabilizedCandidate = stabilized;
			diagnostics.retaining = retaining;
			diagnostics.cost = cost;
		};
		using Candidate = PartitionCandidate;
		const auto prepare = [&](Candidate& candidate) { PreparePartition(candidate, actors, a_width, a_height); };
		const auto admissible = [&](const Candidate& candidate, bool retaining) {
			return !overlaps(candidate.providers) && CoversEligibility(candidate.providers, a_eligibility, a_width, a_height) &&
			       WorthSplitting(candidate.providers, single.Area(), retaining, a_savingsGate);
		};
		Candidate selected;
		for (const auto& cluster : a_state.clusters) {
			if (cluster.owners.empty())
				continue;
			std::vector<std::size_t> group;
			for (std::size_t i = 0; i < actors.size(); ++i)
				if (std::ranges::binary_search(cluster.owners, actors[i].identity))
					group.push_back(i);
			if (group.size() != cluster.owners.size()) {
				selected = {};
				break;
			}
			selected.groups.push_back(std::move(group));
		}
		std::size_t retainedActors = 0;
		for (const auto& group : selected.groups)
			retainedActors += group.size();
		bool retaining = sameLimit && retainedActors == actors.size() && selected.groups.size() >= 2 && selected.groups.size() <= a_regionLimit;
		if (retaining) {
			prepare(selected);
			recordCandidate(selected.providers, true, false);
			retaining = admissible(selected, true);
		}
		if (!retaining) {
			selected = FindPartition(actors, a_width, a_height, a_regionLimit, recordCandidate, admissible);
			if (selected.groups.empty())
				return fallback(diagnostics.savingsRejectedCandidates  ? CharacterMultiRoiReason::InsufficientSavings :
								diagnostics.coverageRejectedCandidates ? CharacterMultiRoiReason::EligibilityBridge :
																		 CharacterMultiRoiReason::NoDisjointSplit);
		}
		std::vector<std::vector<std::uint64_t>> owners(selected.groups.size());
		for (std::size_t i = 0; i < selected.groups.size(); ++i)
			for (auto actor : selected.groups[i])
				owners[i].push_back(actors[actor].identity);
		std::array<bool, kEnabledRegionsPerEye> used{};
		std::vector<std::uint32_t> banks(owners.size(), kEnabledRegionsPerEye);
		for (std::size_t i = 0; i < owners.size(); ++i)
			for (std::uint32_t bank = 0; bank < kEnabledRegionsPerEye; ++bank)
				if (owners[i] == a_state.clusters[bank].owners) {
					banks[i] = bank;
					used[bank] = true;
					break;
				}
		for (auto& bank : banks)
			if (bank == kEnabledRegionsPerEye)
				for (std::uint32_t free = 0; free < kEnabledRegionsPerEye; ++free)
					if (!used[free]) {
						bank = free;
						used[free] = true;
						break;
					}
		std::array<StableCharacterMultiRoi::Cluster, kEnabledRegionsPerEye> next{};
		CharacterComputeRegionPlan result;
		for (std::uint32_t bank = 0; bank < kEnabledRegionsPerEye; ++bank) {
			const auto match = std::ranges::find(banks, bank);
			if (match == banks.end())
				continue;
			const auto i = static_cast<std::size_t>(match - banks.begin());
			auto& cluster = next[bank];
			if (a_state.clusters[bank].owners == owners[i])
				cluster = a_state.clusters[bank];
			else {
				cluster.owners = owners[i];
				cluster.historyKey = HistoryKey(cluster.owners, a_sourceFrame);
			}
			const auto index = result.count++;
			result.regionSlots[index] = bank;
			result.regions[index] = ResolveStableCharacterComputeSubrect(Required(selected.bounds[i], a_width, a_height), a_width, a_height, cluster.stable);
			result.historyKeys[index] = cluster.historyKey;
			result.clusterIdentities[index] = ClusterIdentity(cluster.owners);
			if (!result.regions[index].Fits(a_width, a_height))
				return fallback(CharacterMultiRoiReason::InvalidInput);
			for (std::uint32_t earlier = 0; earlier < index; ++earlier)
				if (result.historyKeys[earlier] == result.historyKeys[index] || result.clusterIdentities[earlier] == result.clusterIdentities[index])
					return fallback(CharacterMultiRoiReason::InvalidInput);
		}
		const bool mergedOverlap = RepairOverlappingHistories(result, next, a_width, a_height, a_sourceFrame);
		if (mergedOverlap && result.count < 2)
			return fallback(CharacterMultiRoiReason::StableRegionsOverlap);
		const auto regions = std::span(result.regions).first(result.count);
		recordCandidate(regions, retaining, true);
		if (overlaps(regions))
			return fallback(CharacterMultiRoiReason::StableRegionsOverlap);
		if (!WorthSplitting(regions, single.Area(), retaining, a_savingsGate))
			return fallback(CharacterMultiRoiReason::InsufficientSavings);
		if (!CoversEligibility(regions, a_eligibility, a_width, a_height))
			return fallback(CharacterMultiRoiReason::EligibilityBridge);
		for (std::uint32_t i = 0; i < result.count; ++i) {
			ComputeSubrect support{};
			for (const auto& rect : a_eligibility) {
				const auto requiredSupport = Required(rect, a_width, a_height);
				if (ContainsComputeSubrect(result.regions[i], requiredSupport))
					support = UnionCharacterComputeSubrect(support, requiredSupport);
			}
			for (const auto& actor : actors)
				if (std::ranges::binary_search(next[result.regionSlots[i]].owners, actor.identity))
					support = UnionCharacterComputeSubrect(support, Required(actor.rect, a_width, a_height));
			result.roi[i] = BuildRoiDescriptor(support, result.regions[i], { a_width, a_height }, true);
		}
		a_state.clusters = std::move(next);
		a_state.cachedReason = a_reason = CharacterMultiRoiReason::Split;
		a_state.cachedPlan = result;
		return result;
	}
}
