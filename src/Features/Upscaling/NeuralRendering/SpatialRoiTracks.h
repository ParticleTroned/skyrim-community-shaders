#pragma once

#include "CharacterMultiRoi.h"

namespace NeuralRendering
{
	struct SpatialRoiTrack
	{
		CharacterRect bounds{};
		std::uint64_t identity = 0;
		bool confident = false;
	};

	/** Category masks carry no actor identity; only unambiguous geometric continuation is retained. */
	[[nodiscard]] inline std::vector<SpatialRoiTrack> MatchSpatialRoiTracks(
		std::span<const CharacterRect> current, std::span<const SpatialRoiTrack> previous,
		std::uint32_t frame, std::uint32_t eyeNamespace)
	{
		const auto related = [](const CharacterRect& a, const CharacterRect& b) {
			const auto left = std::max(a.minX, b.minX), top = std::max(a.minY, b.minY);
			const auto right = std::min(a.maxX, b.maxX), bottom = std::min(a.maxY, b.maxY);
			if (left >= right || top >= bottom)
				return false;
			const auto intersection = std::uint64_t(right - left) * (bottom - top);
			const auto areaA = std::uint64_t(a.maxX - a.minX) * (a.maxY - a.minY);
			const auto areaB = std::uint64_t(b.maxX - b.minX) * (b.maxY - b.minY);
			return intersection >= areaA / 2 + areaA % 2 && intersection >= areaB / 2 + areaB % 2;
		};
		std::vector<SpatialRoiTrack> result;
		for (const auto& bounds : current) {
			const SpatialRoiTrack* match = nullptr;
			std::size_t matches = 0;
			for (const auto& old : previous)
				if (old.identity && related(bounds, old.bounds)) {
					match = &old;
					++matches;
				}
			const bool unique = matches == 1 && std::ranges::count_if(current,
													[&](const auto& rect) { return related(rect, match->bounds); }) == 1;
			const std::array<std::uint64_t, 7> key{ 0x5350415449414C00ull, eyeNamespace, frame,
				bounds.minX, bounds.minY, bounds.maxX, bounds.maxY };
			result.push_back({ bounds, unique ? match->identity : CharacterMultiRoiDetail::ClusterIdentity(key), unique });
		}
		return result;
	}
}
