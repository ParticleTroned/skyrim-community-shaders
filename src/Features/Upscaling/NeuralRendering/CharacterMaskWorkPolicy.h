#pragma once

#include "ComputeSubrect.h"

#include <algorithm>
#include <cstdint>

namespace NeuralRendering
{
	enum class CharacterDepthExtentPolicy
	{
		ExactCapture,
		ContainsActiveInput,
	};

	/** Frozen depth is exact; engine depth can retain its display-size allocation. */
	[[nodiscard]] constexpr bool IsCharacterDepthExtentValid(
		std::uint32_t a_allocationWidth, std::uint32_t a_allocationHeight,
		std::uint32_t a_activeWidth, std::uint32_t a_activeHeight,
		CharacterDepthExtentPolicy a_policy) noexcept
	{
		if (a_activeWidth == 0 || a_activeHeight == 0)
			return false;
		return a_policy == CharacterDepthExtentPolicy::ContainsActiveInput ?
		           a_allocationWidth >= a_activeWidth && a_allocationHeight >= a_activeHeight :
		           a_allocationWidth == a_activeWidth && a_allocationHeight == a_activeHeight;
	}

	// These helpers describe texture work, not provider history. A stale mask
	// region must be cleared even when the new mask/compute ROI has moved away.
	[[nodiscard]] constexpr ComputeSubrect UnionCharacterWorkRects(
		const ComputeSubrect& a_left, const ComputeSubrect& a_right) noexcept
	{
		if (!a_left.IsValid())
			return a_right;
		if (!a_right.IsValid())
			return a_left;
		const auto left = std::min(a_left.baseX, a_right.baseX);
		const auto top = std::min(a_left.baseY, a_right.baseY);
		const auto right = std::max(
			static_cast<std::uint64_t>(a_left.baseX) + a_left.width,
			static_cast<std::uint64_t>(a_right.baseX) + a_right.width);
		const auto bottom = std::max(
			static_cast<std::uint64_t>(a_left.baseY) + a_left.height,
			static_cast<std::uint64_t>(a_right.baseY) + a_right.height);
		if (right > UINT32_MAX || bottom > UINT32_MAX)
			return {};
		return { left, top, static_cast<std::uint32_t>(right - left),
			static_cast<std::uint32_t>(bottom - top) };
	}

	[[nodiscard]] constexpr ComputeSubrect ExpandCharacterWorkRect(
		const ComputeSubrect& a_rect, std::uint32_t a_width,
		std::uint32_t a_height, std::uint32_t a_guard) noexcept
	{
		if (!a_rect.Fits(a_width, a_height))
			return {};
		const auto left = a_rect.baseX > a_guard ? a_rect.baseX - a_guard : 0u;
		const auto top = a_rect.baseY > a_guard ? a_rect.baseY - a_guard : 0u;
		const auto right = std::min<std::uint64_t>(a_width,
			static_cast<std::uint64_t>(a_rect.baseX) + a_rect.width + a_guard);
		const auto bottom = std::min<std::uint64_t>(a_height,
			static_cast<std::uint64_t>(a_rect.baseY) + a_rect.height + a_guard);
		return { left, top, static_cast<std::uint32_t>(right - left),
			static_cast<std::uint32_t>(bottom - top) };
	}
}
