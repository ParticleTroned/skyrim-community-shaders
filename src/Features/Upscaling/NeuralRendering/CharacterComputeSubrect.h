#pragma once

#include "CharacterRegionPolicy.h"
#include "ComputeSubrect.h"

#include <algorithm>
#include <cstdint>
#include <span>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterComputeRoiAlignment = 8;
	inline constexpr std::uint32_t kCharacterComputeRoiGuardPixels = 2;

	/** Builds one guarded, outward-aligned rectangle enclosing every region. */
	[[nodiscard]] inline ComputeSubrect BuildCharacterComputeSubrect(
		std::span<const CharacterRect> a_regions,
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		CharacterRect bounds{};
		for (const auto& region : a_regions)
			bounds = CharacterRegionPolicy::Union(bounds, region);
		if (!bounds.IsValid() || !a_width || !a_height ||
			bounds.maxX > a_width || bounds.maxY > a_height) {
			return {};
		}

		const auto alignDown = [](std::uint32_t a_value) {
			return (a_value / kCharacterComputeRoiAlignment) *
			       kCharacterComputeRoiAlignment;
		};
		const auto alignUpClamped = [](
			std::uint32_t a_value,
			std::uint32_t a_limit) {
			const auto aligned =
				(static_cast<std::uint64_t>(a_value) +
					kCharacterComputeRoiAlignment - 1u) /
				kCharacterComputeRoiAlignment * kCharacterComputeRoiAlignment;
			return static_cast<std::uint32_t>(std::min<std::uint64_t>(
				aligned, a_limit));
		};
		const auto guardedMax = [](
			std::uint32_t a_value,
			std::uint32_t a_limit) {
			return static_cast<std::uint32_t>(std::min<std::uint64_t>(
				static_cast<std::uint64_t>(a_value) +
					kCharacterComputeRoiGuardPixels,
				a_limit));
		};

		const auto left = alignDown(
			bounds.minX > kCharacterComputeRoiGuardPixels ?
				bounds.minX - kCharacterComputeRoiGuardPixels :
				0u);
		const auto top = alignDown(
			bounds.minY > kCharacterComputeRoiGuardPixels ?
				bounds.minY - kCharacterComputeRoiGuardPixels :
				0u);
		const auto right = alignUpClamped(
			guardedMax(bounds.maxX, a_width), a_width);
		const auto bottom = alignUpClamped(
			guardedMax(bounds.maxY, a_height), a_height);
		return {
			.baseX = left,
			.baseY = top,
			.width = right > left ? right - left : 0u,
			.height = bottom > top ? bottom - top : 0u,
		};
	}
}
