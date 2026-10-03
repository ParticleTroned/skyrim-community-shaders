#pragma once

#include "CharacterMultiRoi.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NeuralRendering::MeasuredPlan
{
	/** Separate clocks and residency are constraints, never an additive GPU estimate. */
	struct Cost
	{
		double gpuLowerMs = 0, gpuUpperMs = 0, gpuTailMs = 0;
		double cpuCriticalUpperMs = 0, transitionUpperMs = 0;
		std::uint64_t residentBytes = 0;
		std::uint32_t samples = 0;
		[[nodiscard]] bool Valid() const noexcept
		{
			for (const auto value : { gpuLowerMs, gpuUpperMs, gpuTailMs, cpuCriticalUpperMs, transitionUpperMs })
				if ((std::bit_cast<std::uint64_t>(value) & 0x7ff0000000000000ull) == 0x7ff0000000000000ull || value < 0)
					return false;
			return samples >= 30 && gpuLowerMs > 0 && gpuUpperMs >= gpuLowerMs && gpuTailMs >= gpuUpperMs && residentBytes > 0;
		}
	};
	struct Observation
	{
		std::string key;
		Cost cost{};
	};
	struct Profile
	{
		std::string identity;
		std::vector<Observation> observations;
		std::uint32_t dwellFrames = 30;
		std::uint64_t residentBudgetBytes = 0;
		[[nodiscard]] bool Valid() const noexcept
		{
			if (identity.empty() || observations.empty() || observations.size() > 256 || dwellFrames < 1 || dwellFrames > 600 || !residentBudgetBytes)
				return false;
			for (std::size_t i = 0; i < observations.size(); ++i) {
				if (observations[i].key.empty() || !observations[i].cost.Valid())
					return false;
				for (std::size_t j = 0; j < i; ++j)
					if (observations[i].key == observations[j].key)
						return false;
			}
			return true;
		}
		[[nodiscard]] const Cost* Find(std::string_view key) const noexcept
		{
			for (const auto& observation : observations)
				if (observation.key == key)
					return &observation.cost;
			return nullptr;
		}
	};
	struct Candidate
	{
		std::string key;
		std::uint64_t partition = 0;
		bool coverageValid = false, capacityRejected = false;
	};
	struct State
	{
		std::uint64_t partition = 0;
		std::uint32_t changedFrame = 0;
		std::string identity;
	};
	struct Decision
	{
		std::size_t index = 0;
		const char* reason = "unknown_cost_fallback";
	};
	/** Exact final-plan lookup: unmeasured shapes and identities never extrapolate. */
	[[nodiscard]] inline Decision Select(std::span<const Candidate> candidates, std::size_t fallback,
		std::string_view identity, const Profile* profile, std::uint32_t frame, State& state)
	{
		if (fallback >= candidates.size())
			return { fallback, "invalid_fallback" };
		if (!profile || profile->identity != identity || profile->dwellFrames < 1 || profile->dwellFrames > 600 || !profile->residentBudgetBytes) {
			state = {};
			return { fallback, "unknown_cost_fallback" };
		}
		const auto usable = [&](std::size_t index) {
			const auto& candidate = candidates[index];
			const auto* cost = profile->Find(candidate.key);
			return candidate.coverageValid && !candidate.capacityRejected && cost && cost->Valid() && cost->residentBytes <= profile->residentBudgetBytes;
		};
		std::size_t current = fallback;
		if (state.identity == identity)
			for (std::size_t i = 0; i < candidates.size(); ++i)
				if (candidates[i].partition == state.partition && usable(i))
					current = i;
		if (!usable(current)) {
			state = {};
			return { fallback, "unknown_or_rejected_baseline" };
		}
		const auto* baseline = profile->Find(candidates[current].key);
		const bool retaining = state.identity == identity && state.partition == candidates[current].partition;
		if (retaining && frame >= state.changedFrame && frame - state.changedFrame < profile->dwellFrames)
			return { current, "measured_dwell" };
		std::size_t selected = current;
		double bestUpper = baseline->gpuLowerMs;
		for (std::size_t i = 0; i < candidates.size(); ++i) {
			if (i == current || !usable(i))
				continue;
			const auto& cost = *profile->Find(candidates[i].key);
			const double upper = cost.gpuUpperMs + cost.transitionUpperMs / profile->dwellFrames;
			if (upper < bestUpper && cost.gpuTailMs <= baseline->gpuTailMs && cost.cpuCriticalUpperMs <= baseline->cpuCriticalUpperMs) {
				selected = i;
				bestUpper = upper;
			}
		}
		if (!retaining || selected != current || frame < state.changedFrame)
			state = { candidates[selected].partition, frame, std::string(identity) };
		return { selected, selected == current ? "measured_uncertainty_or_constraints" : "measured_final_plan" };
	}

	struct Partition
	{
		CharacterComputeRegionPlan plan{};
		std::array<std::uint32_t, kEnabledRegionsPerEye> members{};
		std::array<std::uint64_t, kEnabledRegionsPerEye> histories{}, identities{};
		std::uint64_t id = 0;
	};
	/** Merge complete provider work while retaining untouched semantic history banks. */
	[[nodiscard]] inline std::optional<Partition> Merge(const Partition& source, std::uint32_t first, std::uint32_t second,
		std::uint32_t width, std::uint32_t height, bool intermediate = false)
	{
		if (first >= second || second >= source.plan.count)
			return std::nullopt;
		auto result = source;
		auto& plan = result.plan;
		plan.regions[first] = UnionCharacterComputeSubrect(plan.regions[first], plan.regions[second]);
		if (!plan.regions[first].Fits(width, height))
			return std::nullopt;
		result.members[first] |= result.members[second];
		ComputeSubrect support = UnionCharacterComputeSubrect(plan.roi[first].samplingSupport.value_or(plan.regions[first]),
			plan.roi[second].samplingSupport.value_or(plan.regions[second]));
		plan.roi[first] = BuildRoiDescriptor(support, plan.regions[first], { width, height }, true);
		// A merged ownership episode cannot inherit either source's native history.
		const auto groupKey = [&](const auto& keys) {
			std::array<std::uint64_t, kEnabledRegionsPerEye> selected{};
			std::size_t count = 0;
			for (std::uint32_t i = 0; i < kEnabledRegionsPerEye; ++i)
				if (result.members[first] & (1u << i))
					selected[count++] = keys[i];
			const auto values = std::span(selected).first(count);
			std::ranges::sort(values);
			return CharacterMultiRoiDetail::ClusterIdentity(values);
		};
		plan.historyKeys[first] = groupKey(result.histories);
		plan.clusterIdentities[first] = groupKey(result.identities);
		plan.identityConfident[first] = false;
		for (std::uint32_t i = second + 1; i < plan.count; ++i) {
			plan.regions[i - 1] = plan.regions[i];
			plan.roi[i - 1] = plan.roi[i];
			plan.historyKeys[i - 1] = plan.historyKeys[i];
			plan.clusterIdentities[i - 1] = plan.clusterIdentities[i];
			plan.regionSlots[i - 1] = plan.regionSlots[i];
			plan.identityConfident[i - 1] = plan.identityConfident[i];
			result.members[i - 1] = result.members[i];
		}
		--plan.count;
		result.id = 0;
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			result.id |= std::uint64_t(result.members[i]) << (i * 8u);
			for (std::uint32_t j = 0; j < i; ++j)
				if (!intermediate && (CharacterComputeRegionsOverlap(plan.regions[i], plan.regions[j]) ||
										 plan.historyKeys[i] == plan.historyKeys[j] || plan.clusterIdentities[i] == plan.clusterIdentities[j]))
					return std::nullopt;
		}
		return result;
	}
	/** Bounded lookahead includes the current split and every reached valid local merge. */
	[[nodiscard]] inline std::vector<Partition> Candidates(const CharacterComputeRegionPlan& plan, std::uint32_t width, std::uint32_t height)
	{
		if (!plan.count || plan.count > kEnabledRegionsPerEye)
			return {};
		Partition initial{ plan };
		initial.histories = plan.historyKeys;
		initial.identities = plan.clusterIdentities;
		for (std::uint32_t i = 0; i < plan.count; ++i) {
			initial.members[i] = 1u << i;
			initial.id |= std::uint64_t(initial.members[i]) << (i * 8u);
		}
		std::vector<Partition> result{ initial };
		auto enclosure = initial;
		while (enclosure.plan.count > 1) {
			const auto merged = Merge(enclosure, 0, 1, width, height, true);
			if (!merged)
				return result;
			enclosure = *merged;
		}
		if (plan.count > 1)
			result.push_back(enclosure);
		for (std::size_t cursor = 0; cursor < result.size() && result.size() < 256; ++cursor) {
			const auto parent = result[cursor];
			for (std::uint32_t i = 0; i < parent.plan.count && result.size() < 256; ++i)
				for (std::uint32_t j = i + 1; j < parent.plan.count && result.size() < 256; ++j)
					if (auto next = Merge(parent, i, j, width, height); next && std::ranges::none_of(result, [&](const auto& old) { return old.id == next->id; }))
						result.push_back(*next);
		}
		return result;
	}
}
