#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRDepthCullingTemporalPolicy.h"

#	include <array>
#	include <cstddef>
#	include <cstdint>
#	include <optional>
#	include <span>

namespace VRHybridCullingDiagnostics
{

	inline constexpr std::array Reasons{ "not_tested", "occluded", "clip_crossing", "viewport_guard",
		"invalid_input", "depth_budget", "finest_unresolved", "stack_capacity", "nearest_unresolved",
		"viewport_offscreen", "viewport_partial" };

	/** One diagnostic shader record per native-indexed object; work sums both eyes. */
	struct Record
	{
		std::uint32_t reasons, depthLoads, faceRegions, faceTriangles;
		std::uint32_t planeProofs, polygonClips, faceBiasOnlyProofs, triangleBiasOnlyProofs;
	};
	static_assert(sizeof(Record) == 32 && offsetof(Record, planeProofs) == 16);

	struct Totals
	{
		std::array<std::uint64_t, Reasons.size()> eyeReasons{};
		std::uint64_t objects = 0, depthLoads = 0, faceRegions = 0, faceTriangles = 0;
		std::uint64_t planeProofs = 0, polygonClips = 0, faceBiasOnlyProofs = 0, triangleBiasOnlyProofs = 0;
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
				((first == 2 || first == 3 || first == 9 || first == 10) && record.depthLoads != 0) ||
				(first == 8 && (record.depthLoads < 4 || record.depthLoads > 5 || record.faceRegions != 0 || record.faceTriangles != 0)) ||
				(second == 8 && (record.depthLoads < 8 || record.depthLoads > 69)) ||
				(second == 5 && record.depthLoads < 68) || record.depthLoads > 128 || record.faceRegions > record.depthLoads ||
				record.faceTriangles > record.faceRegions * 12 || record.faceBiasOnlyProofs > record.faceRegions * 6 ||
				record.faceTriangles > (record.faceRegions * 6 - record.faceBiasOnlyProofs) * 2 ||
				static_cast<std::uint64_t>(record.planeProofs) + record.polygonClips + record.triangleBiasOnlyProofs > record.faceTriangles)
				return std::nullopt;
			++result.eyeReasons[first];
			++result.eyeReasons[second];
			++result.objects;
			result.depthLoads += record.depthLoads;
			result.faceRegions += record.faceRegions;
			result.faceTriangles += record.faceTriangles;
			result.planeProofs += record.planeProofs;
			result.polygonClips += record.polygonClips;
			result.faceBiasOnlyProofs += record.faceBiasOnlyProofs;
			result.triangleBiasOnlyProofs += record.triangleBiasOnlyProofs;
		}
		return result;
	}
}

#endif
