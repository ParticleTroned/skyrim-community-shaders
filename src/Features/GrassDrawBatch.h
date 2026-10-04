#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace GrassDrawBatch
{
	inline constexpr uint32_t kInstanceStride = 32;
	inline constexpr uint32_t kFadeGroups = 960;
	inline constexpr uint32_t kMaxInstances = (32 * 1024 * 1024) / kInstanceStride;

	struct Group
	{
		uint32_t count;
		uint32_t fadeIndex;
	};

	struct Range
	{
		uint32_t end;
		uint32_t fadeIndex;
	};

	/** @brief Build complete, ordered ranges, rejecting the entire batch on overflow. */
	inline bool BuildRanges(std::span<const Group> groups, std::vector<Range>& ranges, uint32_t& instances)
	{
		ranges.clear();
		instances = 0;
		if (groups.size() < 2 || groups.size() > kFadeGroups)
			return false;
		uint64_t total = 0;
		for (const auto& group : groups) {
			total += group.count;
			if (!group.count || group.fadeIndex >= kFadeGroups || total > kMaxInstances) {
				ranges.clear();
				return false;
			}
			ranges.push_back({ static_cast<uint32_t>(total), group.fadeIndex });
		}
		instances = static_cast<uint32_t>(total);
		return true;
	}
}
