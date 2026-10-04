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
		"viewport_offscreen", "viewport_partial", "eye_crossing", "near_crossing", "far_crossing" };

	/** One diagnostic shader record per native-indexed object; work sums both eyes. */
	struct Record
	{
		std::uint32_t reasons, depthLoads, faceRegions, faceTriangles;
		std::uint32_t planeProofs, polygonClips, faceBiasOnlyProofs, triangleBiasOnlyProofs;
		std::uint32_t clipPlanes, skippedClipPlanes, planeBuilds, planeReuses;
		std::uint32_t refinedCells, sourcePixels, resolvedCells, sourceWitnesses;
		std::uint32_t triangleRegionTests, disjointTriangles, emptyClips, clipVertexVisits;
		std::uint32_t directTests, directProofs, directFallbacks, farClampedVertices;
	};
	static_assert(sizeof(Record) == 96 && offsetof(Record, planeProofs) == 16);

	struct Totals
	{
		std::array<std::uint64_t, Reasons.size()> eyeReasons{};
		std::uint64_t objects = 0, depthLoads = 0, faceRegions = 0, faceTriangles = 0;
		std::uint64_t planeProofs = 0, polygonClips = 0, faceBiasOnlyProofs = 0, triangleBiasOnlyProofs = 0;
		std::uint64_t clipPlanes = 0, skippedClipPlanes = 0, planeBuilds = 0, planeReuses = 0;
		std::uint64_t refinedCells = 0, sourcePixels = 0, resolvedCells = 0, sourceWitnesses = 0;
		std::uint64_t triangleRegionTests = 0, disjointTriangles = 0, emptyClips = 0, clipVertexVisits = 0;
		std::uint64_t directTests = 0, directProofs = 0, directFallbacks = 0, farClampedVertices = 0;
	};

	/** Keep accumulated work complete when a diagnostic field is added. */
	inline constexpr std::array WorkMembers{
		&Totals::objects, &Totals::depthLoads, &Totals::faceRegions, &Totals::faceTriangles,
		&Totals::planeProofs, &Totals::polygonClips, &Totals::faceBiasOnlyProofs, &Totals::triangleBiasOnlyProofs,
		&Totals::clipPlanes, &Totals::skippedClipPlanes, &Totals::planeBuilds, &Totals::planeReuses,
		&Totals::refinedCells, &Totals::sourcePixels, &Totals::resolvedCells, &Totals::sourceWitnesses,
		&Totals::triangleRegionTests, &Totals::disjointTriangles, &Totals::emptyClips, &Totals::clipVertexVisits,
		&Totals::directTests, &Totals::directProofs, &Totals::directFallbacks, &Totals::farClampedVertices
	};

	inline void Accumulate(Totals& a_total, const Totals& a_sample)
	{
		for (auto field : WorkMembers)
			a_total.*field += a_sample.*field;
		for (std::size_t index = 0; index < a_total.eyeReasons.size(); ++index)
			a_total.eyeReasons[index] += a_sample.eyeReasons[index];
	}

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
				((first == 2 || first == 3 || first == 9 || first == 10 || first >= 11) && record.depthLoads != 0) ||
				(first == 8 && record.depthLoads == 6 && record.sourceWitnesses == 0) ||
				(first == 8 && (record.depthLoads < 4 || record.depthLoads > 6 || record.faceRegions != 0 || record.faceTriangles != 0)) ||
				(second == 8 && record.depthLoads == 70 && record.sourceWitnesses == 0) ||
				(second == 8 && (record.depthLoads < 8 || record.depthLoads > 70)) ||
				(second == 5 && record.depthLoads < 68) || record.depthLoads > 128 || record.faceRegions > record.depthLoads ||
				record.faceTriangles > record.faceRegions * 12 || record.faceBiasOnlyProofs > record.faceRegions * 6 ||
				record.faceTriangles > (record.faceRegions * 6 - record.faceBiasOnlyProofs) * 2 ||
				static_cast<std::uint64_t>(record.planeProofs) + record.polygonClips + record.disjointTriangles + record.directProofs + record.triangleBiasOnlyProofs > record.faceTriangles)
				return std::nullopt;
			if (static_cast<std::uint64_t>(record.clipPlanes) + record.skippedClipPlanes > static_cast<std::uint64_t>(record.polygonClips) * 4 ||
				static_cast<std::uint64_t>(record.planeBuilds) + record.planeReuses > record.faceTriangles ||
				record.sourcePixels > record.depthLoads || record.sourceWitnesses > 2 || record.resolvedCells > record.refinedCells ||
				record.refinedCells > record.faceRegions || record.resolvedCells > record.sourcePixels ||
				record.emptyClips > record.polygonClips || record.disjointTriangles > record.triangleRegionTests ||
				record.triangleRegionTests > record.faceTriangles || record.farClampedVertices > 16 ||
				static_cast<std::uint64_t>(record.directProofs) + record.directFallbacks != record.directTests ||
				record.directTests > record.triangleRegionTests - record.disjointTriangles)
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
			result.clipPlanes += record.clipPlanes;
			result.skippedClipPlanes += record.skippedClipPlanes;
			result.planeBuilds += record.planeBuilds;
			result.planeReuses += record.planeReuses;
			result.refinedCells += record.refinedCells;
			result.sourcePixels += record.sourcePixels;
			result.resolvedCells += record.resolvedCells;
			result.sourceWitnesses += record.sourceWitnesses;
			result.triangleRegionTests += record.triangleRegionTests;
			result.disjointTriangles += record.disjointTriangles;
			result.emptyClips += record.emptyClips;
			result.clipVertexVisits += record.clipVertexVisits;
			result.directTests += record.directTests;
			result.directProofs += record.directProofs;
			result.directFallbacks += record.directFallbacks;
			result.farClampedVertices += record.farClampedVertices;
		}
		return result;
	}

	inline constexpr std::array Outcomes{ "both_visible", "native_only_hidden", "hiz_only_hidden", "both_hidden" };
	struct MatchedTotals
	{
		std::uint64_t batches = 0;
		std::array<std::uint64_t, Outcomes.size()> beforeRecovery{}, afterRecovery{};
		std::array<std::uint64_t, Reasons.size()> nativeOnlyReasons{}, hizOnlyReasons{};
		Totals work{};
	};

	/** Compare the same native indices; caller must validate bounds, frame, source and camera identity. */
	inline std::optional<MatchedTotals> SummarizeMatched(std::span<const Record> a_records,
		std::span<const std::uint32_t> a_hiz, std::span<const std::uint32_t> a_nativeBefore, std::span<const std::uint32_t> a_nativeAfter)
	{
		const auto work = Summarize(a_records, a_hiz);
		if (!work || a_nativeBefore.size() != a_hiz.size() || a_nativeAfter.size() != a_hiz.size())
			return std::nullopt;
		MatchedTotals result{ .work = *work };
		for (std::size_t index = 0; index < a_hiz.size(); ++index) {
			if (a_nativeBefore[index] > 1 || a_nativeAfter[index] > 1)
				return std::nullopt;
			const auto before = (a_nativeBefore[index] == 0 ? 1u : 0u) + (a_hiz[index] == 0 ? 2u : 0u);
			const auto after = (a_nativeAfter[index] == 0 ? 1u : 0u) + (a_hiz[index] == 0 ? 2u : 0u);
			++result.beforeRecovery[before];
			++result.afterRecovery[after];
			const auto first = a_records[index].reasons & 255;
			const auto reason = first == 1 ? a_records[index].reasons >> 8 : first;
			if (after == 1)
				++result.nativeOnlyReasons[reason];
			if (after == 2)
				++result.hizOnlyReasons[reason];
		}
		return result;
	}
}

#endif
