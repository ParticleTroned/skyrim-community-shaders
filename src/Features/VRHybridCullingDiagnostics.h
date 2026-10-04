#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRDepthCullingTemporalPolicy.h"

#	include <array>
#	include <cstdint>
#	include <optional>
#	include <span>

namespace VRHybridCullingDiagnostics
{

	inline constexpr std::array Reasons{ "not_tested", "occluded", "clip_crossing", "viewport_guard",
		"invalid_input", "depth_budget", "finest_unresolved", "stack_capacity" };

	/** One diagnostic shader record per native-indexed object; work sums both eyes. */
	struct Record
	{
		std::uint32_t reasons, depthLoads, faceRegions, faceTriangles;
	};
	static_assert(sizeof(Record) == 16);

	struct Totals
	{
		std::array<std::uint64_t, Reasons.size()> eyeReasons{};
		std::uint64_t objects = 0, depthLoads = 0, faceRegions = 0, faceTriangles = 0;
	};

	/** Reject malformed or misattributed records before publishing any batch totals. */
	inline std::optional<Totals> Summarize(std::span<const Record> a_records, std::span<const std::uint32_t> a_visibility)
	{
		if (a_records.empty() || a_records.size() != a_visibility.size() || a_records.size() > VRDepthCullingTemporalPolicy::kMaximumObjects)
			return std::nullopt;
		Totals result{};
		for (std::size_t index = 0; index < a_records.size(); ++index) {
			const auto& record = a_records[index];
			const auto first = record.reasons & 255, second = record.reasons >> 8;
			if (a_visibility[index] > 1 || first == 0 || first >= Reasons.size() || second >= Reasons.size() ||
				(first != 1 && second != 0) || (first == 1 && second == 0) ||
				((first == 1 && second == 1) != (a_visibility[index] == 0)) ||
				(second == 0 && record.depthLoads > 64) || (first == 5 && record.depthLoads != 64) ||
				(second == 5 && record.depthLoads < 68) || record.depthLoads > 128 || record.faceRegions > record.depthLoads || record.faceTriangles > record.faceRegions * 12)
				return std::nullopt;
			++result.eyeReasons[first];
			++result.eyeReasons[second];
			++result.objects;
			result.depthLoads += record.depthLoads;
			result.faceRegions += record.faceRegions;
			result.faceTriangles += record.faceTriangles;
		}
		return result;
	}
}

#endif
