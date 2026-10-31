#pragma once

#include "GrassPolicy.h"
#include <functional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace GrassPreparation
{
	struct Run
	{
		size_t source, firstGroup, lastGroup;
		uint32_t record, count;
		std::array<float, 3> lo{}, hi{};
		float minVariance, maxVariance, basis;
		bool known;
	};

	/** @brief Prepare the record layout from captured groups without accessing native state. */
	template <class GetSource>
	void BuildRuns(std::vector<Run>& runs, size_t sourceCount, GetSource getSource)
	{
		uint32_t record = 0;
		for (size_t source = 0; source < sourceCount; ++source) {
			const auto& groups = getSource(source).groups;
			for (size_t index = 0; index < groups.size(); ++index) {
				const auto& group = groups[index];
				if (!runs.empty() && runs.back().source == source && runs.back().known && group.boundsKnown &&
					groups[runs.back().firstGroup].birthTime == group.birthTime) {
					auto& run = runs.back();
					run.lastGroup = index;
					run.count += group.count;
					for (size_t axis = 0; axis < 3; ++axis) {
						run.lo[axis] = std::min(run.lo[axis], group.lo[axis]);
						run.hi[axis] = std::max(run.hi[axis], group.hi[axis]);
					}
					run.minVariance = std::min(run.minVariance, group.minVariance);
					run.maxVariance = std::max(run.maxVariance, group.maxVariance);
					run.basis = std::max(run.basis, group.maxBasisNorm);
				} else
					runs.push_back({ source, index, index, record, group.count, group.lo, group.hi, group.minVariance, group.maxVariance, group.maxBasisNorm, group.boundsKnown });
				record += group.count;
			}
		}
	}

	struct Bucket
	{
		std::vector<size_t> sources;
		std::vector<Run> runs;
		uint32_t instances = 0;
	};

	/** @brief Sort and group an immutable snapshot; the result owns indices and record layouts only. */
	template <class Source, class Hash, class Compatible>
	std::vector<Bucket> Build(std::span<const Source> sources, Hash hash, Compatible compatible)
	{
		if (sources.size() > GrassPolicy::kMaxFrameSources)
			throw std::length_error("Grass preparation source capacity exceeded");
		std::vector<size_t> order;
		order.reserve(sources.size());
		size_t slices = 0;
		for (size_t i = 0; i < sources.size(); ++i) {
			if (sources[i].groups.size() > GrassPolicy::kMaxFrameSlices - slices)
				throw std::length_error("Grass preparation group capacity exceeded");
			slices += sources[i].groups.size();
			order.push_back(i);
		}
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return std::less<>{}(sources[a].identity, sources[b].identity);
		});
		std::vector<Bucket> buckets;
		std::unordered_multimap<size_t, size_t> candidates;
		for (auto sourceIndex : order) {
			const auto& source = sources[sourceIndex];
			uint32_t count = 0;
			for (const auto& group : source.groups) {
				if (group.count > GrassPolicy::kMaxBatchInstances - count)
					throw std::length_error("Grass preparation instance capacity exceeded");
				count += group.count;
			}
			const auto key = hash(source);
			size_t index = buckets.size();
			if (source.queue && source.queue->batching) {
				const auto [begin, end] = candidates.equal_range(key);
				for (auto candidate = begin; candidate != end; ++candidate) {
					const auto& bucket = buckets[candidate->second];
					const auto& first = sources[bucket.sources.front()];
					if (first.queue == source.queue && compatible(first, source) && bucket.instances <= GrassPolicy::kMaxBatchInstances - count) {
						index = candidate->second;
						break;
					}
				}
			}
			if (index == buckets.size()) {
				buckets.emplace_back();
				candidates.emplace(key, index);
			}
			buckets[index].instances += count;
			buckets[index].sources.push_back(sourceIndex);
		}
		for (auto& bucket : buckets)
			BuildRuns(bucket.runs, bucket.sources.size(), [&](size_t index) -> const Source& { return sources[bucket.sources[index]]; });
		return buckets;
	}
}
