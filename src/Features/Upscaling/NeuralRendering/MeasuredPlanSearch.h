#pragma once

#include "CharacterMaskRoi.h"
#include "MeasuredPlanPolicy.h"
#include "PipelinePolicy.h"

namespace NeuralRendering::MeasuredPlan
{
	inline constexpr std::size_t kMaximumCandidatesPerEye = 16;
	inline constexpr std::size_t kAlternativesPerCount = 2;
	inline constexpr std::size_t kMaximumCoverageRectangles =
		(kCharacterMaskRoiMaximumExtent / kCharacterMaskRoiTileSize) * (kCharacterMaskRoiMaximumExtent / kCharacterMaskRoiTileSize);
	inline constexpr std::size_t kCoverageCheckBudget = 2u * 1024u * 1024u;

	/** Immutable coverage from the same prepared mask; never delayed GPU bounds. */
	struct SearchInput
	{
		std::vector<CharacterMultiRoiActor> actors;
		std::vector<CharacterRect> eligibility;
		ComputeSubrect enclosure{};
		std::uint64_t generation = 0;
		std::uint32_t sourceFrame = 0, logicalSlot = 0, width = 0, height = 0;
		std::uint32_t regionLimit = 1;
		bool gpuCoverage = false;

		[[nodiscard]] bool Matches(std::uint32_t frame, std::uint64_t epoch, std::uint32_t slot) const noexcept
		{
			return sourceFrame == frame && generation == epoch && logicalSlot == slot;
		}
	};

	struct SearchCandidate
	{
		CharacterComputeRegionPlan plan{};
		std::uint64_t id = 0;
		const char* operation = "keep";
	};

	struct SearchResult
	{
		std::vector<SearchCandidate> candidates;
		std::uint32_t visited = 0, rejected = 0;
		std::size_t coverageChecks = 0;
		bool inputValid = false, truncated = false;
	};

	/** Geometry belongs to partition identity because changing an anchor changes history. */
	[[nodiscard]] inline std::uint64_t SearchIdentity(const CharacterComputeRegionPlan& plan, const ComputeSubrect& enclosure)
	{
		std::uint64_t key = 1469598103934665603ull;
		const auto add = [&](std::uint64_t value) { key = (key ^ value) * 1099511628211ull; };
		add(plan.count);
		for (std::uint32_t i = 0; i < std::max(1u, plan.count); ++i) {
			const auto& rect = plan.count ? plan.regions[i] : enclosure;
			for (auto value : { rect.baseX, rect.baseY, rect.width, rect.height }) add(value);
			if (plan.count) {
				add(plan.regionSlots[i]);
				add(plan.clusterIdentities[i]);
				add(plan.historyKeys[i]);
			}
		}
		return key ? key : 1u;
	}

	[[nodiscard]] inline bool ValidSearchInput(const SearchInput& input)
	{
		if (input.width > kCharacterMaskRoiMaximumExtent || input.height > kCharacterMaskRoiMaximumExtent || !input.enclosure.Fits(input.width, input.height) || !input.generation || input.logicalSlot >= kLogicalFeatureSlotCount ||
			input.regionLimit < 1 || input.regionLimit > kEnabledRegionsPerEye || input.actors.empty() ||
			input.actors.size() > CharacterMultiRoiDetail::kMaximumActors || input.eligibility.empty() || input.eligibility.size() > kMaximumCoverageRectangles)
			return false;
		for (std::size_t i = 0; i < input.actors.size(); ++i) {
			const auto& actor = input.actors[i];
			if (!actor.identity || !CharacterMultiRoiDetail::Required(actor.rect, input.width, input.height).Fits(input.width, input.height))
				return false;
			for (std::size_t j = 0; j < i; ++j)
				if (input.actors[j].identity == actor.identity)
					return false;
		}
		for (const auto& rect : input.eligibility)
			if (!ContainsComputeSubrect(input.enclosure, CharacterMultiRoiDetail::Required(rect, input.width, input.height)))
				return false;
		return true;
	}

	/** Admission uses guarded support and final provider rectangles, with no area heuristic. */
	[[nodiscard]] inline bool SearchCoverageValid(const CharacterComputeRegionPlan& plan, const SearchInput& input)
	{
		if (!plan.count || plan.count > input.regionLimit || plan.count > kEnabledRegionsPerEye)
			return false;
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			if (!ContainsComputeSubrect(input.enclosure, plan.regions[i]) || !QualifiedExperimentalContextGeometry(plan.regions[i]) ||
				!plan.historyKeys[i] || !plan.clusterIdentities[i] || plan.regionSlots[i] >= kEnabledRegionsPerEye ||
				!GetRoiDescriptorViolation(plan.roi[i], plan.regions[i], { input.width, input.height }).empty())
				return false;
			for (std::uint32_t j = 0; j < i; ++j)
				if (CharacterComputeRegionsOverlap(plan.regions[i], plan.regions[j]) || plan.regionSlots[i] == plan.regionSlots[j] ||
					plan.historyKeys[i] == plan.historyKeys[j] || plan.clusterIdentities[i] == plan.clusterIdentities[j])
					return false;
		}
		return CharacterMultiRoiDetail::CoversEligibility(std::span(plan.regions).first(plan.count), input.eligibility, input.width, input.height);
	}

	/** Membership, rather than rectangle rank, retains existing history and physical banks. */
	[[nodiscard]] inline CharacterComputeRegionPlan BuildSearchPartition(const CharacterMultiRoiDetail::PartitionCandidate& candidate,
		const SearchInput& input, const CharacterComputeRegionPlan& current)
	{
		CharacterComputeRegionPlan plan;
		plan.count = static_cast<std::uint32_t>(candidate.groups.size());
		if (!plan.count || plan.count > input.regionLimit || plan.count > kEnabledRegionsPerEye)
			return {};
		plan.spatialTracks = input.gpuCoverage;
		std::array<bool, kEnabledRegionsPerEye> used{};
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			std::vector<std::uint64_t> owners;
			for (auto actor : candidate.groups[i]) owners.push_back(input.actors[actor].identity);
			std::ranges::sort(owners);
			plan.clusterIdentities[i] = CharacterMultiRoiDetail::ClusterIdentity(owners);
			const std::array episode{ std::uint64_t{ 0x4D45415355524544ull }, input.generation, plan.clusterIdentities[i] };
			plan.historyKeys[i] = CharacterMultiRoiDetail::ClusterIdentity(episode);
			plan.regionSlots[i] = kEnabledRegionsPerEye;
			plan.regions[i] = candidate.providers[i];
			auto support = CharacterMultiRoiDetail::Required(candidate.bounds[i], input.width, input.height);
			for (const auto& rect : input.eligibility) {
				const auto required = CharacterMultiRoiDetail::Required(rect, input.width, input.height);
				if (ContainsComputeSubrect(plan.regions[i], required))
					support = UnionCharacterComputeSubrect(support, required);
			}
			plan.roi[i] = BuildRoiDescriptor(support, plan.regions[i], { input.width, input.height }, true);
			for (std::uint32_t old = 0; old < current.count; ++old)
				if (current.clusterIdentities[old] == plan.clusterIdentities[i]) {
					plan.historyKeys[i] = current.historyKeys[old];
					plan.regionSlots[i] = current.regionSlots[old];
					plan.identityConfident[i] = current.identityConfident[old];
					used[plan.regionSlots[i]] = true;
					break;
				}
		}
		for (std::uint32_t i = 0; i < plan.count; ++i)
			if (plan.regionSlots[i] == kEnabledRegionsPerEye)
				for (std::uint32_t slot = 0; slot < kEnabledRegionsPerEye; ++slot)
					if (!used[slot]) {
						plan.regionSlots[i] = slot;
						used[slot] = true;
						break;
					}
		return plan;
	}

	/** Re-anchor only to the already guarded requirement, retaining ordinary provider headroom. */
	[[nodiscard]] inline std::optional<CharacterComputeRegionPlan> Reanchor(const CharacterComputeRegionPlan& current, const SearchInput& input)
	{
		if (!current.count)
			return std::nullopt;
		auto plan = current;
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			if (!current.roi[i].samplingSupport)
				return std::nullopt;
			plan.regions[i] = BuildCharacterProviderComputeSubrect(*current.roi[i].samplingSupport, input.width, input.height);
			plan.roi[i] = BuildRoiDescriptor(current.roi[i].samplingSupport, plan.regions[i], { input.width, input.height }, true);
		}
		return SearchCoverageValid(plan, input) ? std::optional{ plan } : std::nullopt;
	}

	/** Bounded keep/split/merge/re-anchor search; unscored geometry never implies cheap inference. */
	[[nodiscard]] inline SearchResult Search(const CharacterComputeRegionPlan& current, const ComputeSubrect& enclosure,
		std::uint32_t width, std::uint32_t height, const SearchInput* input)
	{
		SearchResult result;
		if (!enclosure.Fits(width, height) || !GetCharacterRegionSubmissionViolation(0, current, enclosure, width, height, true).empty())
			return result;
		const auto append = [&](const CharacterComputeRegionPlan& plan, const char* operation) {
			const auto id = SearchIdentity(plan, enclosure);
			if (std::ranges::any_of(result.candidates, [&](const auto& old) { return old.id == id; }))
				return;
			if (result.candidates.size() == kMaximumCandidatesPerEye) {
				result.truncated = true;
				return;
			}
			result.candidates.push_back({ plan, id, operation });
		};
		append(current, "keep");
		const auto merges = Candidates(current, width, height);
		result.inputValid = input && input->enclosure == enclosure && input->width == width && input->height == height && ValidSearchInput(*input);
		std::array<std::vector<CharacterComputeRegionPlan>, kEnabledRegionsPerEye> splits;
		if (result.inputValid) {
			auto ordered = *input;
			std::ranges::sort(ordered.actors, {}, &CharacterMultiRoiActor::identity);
			const auto anchorWork = ordered.eligibility.size() * current.count;
			result.coverageChecks += anchorWork;
			if (const auto anchor = Reanchor(current, ordered))
				append(*anchor, "reanchor");
			const auto retain = [&](const CharacterMultiRoiDetail::PartitionCandidate& candidate) {
				const auto work = 2u * ordered.eligibility.size() * candidate.groups.size();
				if (work > kCoverageCheckBudget - result.coverageChecks) {
					result.truncated = true;
					return;
				}
				result.coverageChecks += work;
				auto plan = BuildSearchPartition(candidate, ordered, current);
				if (!SearchCoverageValid(plan, ordered)) {
					++result.rejected;
					return;
				}
				auto& count = splits[plan.count - 1];
				const auto id = SearchIdentity(plan, enclosure);
				if (std::ranges::any_of(count, [&](const auto& old) { return SearchIdentity(old, enclosure) == id; }))
					return;
				count.push_back(std::move(plan));
				std::ranges::stable_sort(count, [&](const auto& a, const auto& b) {
					return CharacterRegionPixels(a) != CharacterRegionPixels(b) ? CharacterRegionPixels(a) < CharacterRegionPixels(b) :
					                                                              SearchIdentity(a, enclosure) < SearchIdentity(b, enclosure);
				});
				if (count.size() > kAlternativesPerCount) {
					count.pop_back();
					result.truncated = true;
				}
			};
			CharacterMultiRoiDetail::PartitionCandidate whole;
			whole.groups.emplace_back();
			for (std::size_t i = 0; i < ordered.actors.size(); ++i) whole.groups.front().push_back(i);
			CharacterMultiRoiDetail::PreparePartition(whole, ordered.actors, width, height);
			retain(whole);
			const auto cutBudget = static_cast<std::uint32_t>(std::min<std::size_t>(CharacterMultiRoiDetail::kPartitionCandidateBudget,
				(kCoverageCheckBudget - result.coverageChecks) / (2u * ordered.eligibility.size() * ordered.regionLimit)));
			result.visited = CharacterMultiRoiDetail::VisitPartitions(ordered.actors, width, height, ordered.regionLimit, retain, cutBudget);
			result.truncated |= result.visited == cutBudget;
		}
		// Reserve representation for each count before filling the bounded joint cost search.
		if (merges.size() > 1)
			append(merges[1].plan, "local_merge");
		for (const auto& count : splits)
			if (!count.empty())
				append(count.front(), count.front().count == 1 ? "reanchor" : "split");
		for (const auto& count : splits)
			if (count.size() > 1)
				append(count[1], "split");
		for (std::size_t i = 2; i < merges.size(); ++i) append(merges[i].plan, "local_merge");
		return result;
	}
}
