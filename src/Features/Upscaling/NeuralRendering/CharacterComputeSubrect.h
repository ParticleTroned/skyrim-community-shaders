#pragma once

#include "CharacterMaskWorkPolicy.h"
#include "CharacterRegionPolicy.h"
#include "ComputeSubrect.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterComputeRoiAlignment = 8;
	inline constexpr std::uint32_t kCharacterComputeRoiGuardPixels = 2;
	inline constexpr std::uint32_t kCharacterProviderRoiAlignment = 64;
	inline constexpr std::uint32_t kCharacterProviderRoiMinimumHeadroomPixels = 32;
	inline constexpr std::uint32_t kCharacterProviderRoiMaximumHeadroomPixels = 96;
	inline constexpr std::uint32_t kCharacterProviderRoiMotionAllowancePixels = 16;
	inline constexpr std::uint32_t kCharacterProviderRoiHeadroomDivisor = 8;
	inline constexpr std::uint32_t kCharacterProviderRoiHistoryFrames = 60;
	inline constexpr std::uint32_t kCharacterProviderRoiShrinkDelayFrames = 30;

	struct StableCharacterComputeSubrect
	{
		ComputeSubrect provider{};
		std::array<ComputeSubrect, kCharacterProviderRoiHistoryFrames> recentRequired{};
		std::uint32_t recentCursor = 0;
		std::uint32_t recentCount = 0;
		std::uint32_t framesSinceContraction = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	[[nodiscard]] inline constexpr std::uint32_t ResolveCharacterProviderRoiHeadroom(
		std::uint32_t a_extent) noexcept
	{
		return std::clamp(
			a_extent / kCharacterProviderRoiHeadroomDivisor +
				kCharacterProviderRoiMotionAllowancePixels,
			kCharacterProviderRoiMinimumHeadroomPixels,
			kCharacterProviderRoiMaximumHeadroomPixels);
	}

	/** Both inputs are eye-local, already validated rectangles. */
	[[nodiscard]] inline constexpr ComputeSubrect UnionCharacterComputeSubrect(
		const ComputeSubrect& a_left,
		const ComputeSubrect& a_right) noexcept
	{
		return UnionCharacterWorkRects(a_left, a_right);
	}

	[[nodiscard]] inline constexpr bool ContainsComputeSubrect(
		const ComputeSubrect& a_outer,
		const ComputeSubrect& a_inner) noexcept
	{
		const auto outerRight =
			static_cast<std::uint64_t>(a_outer.baseX) + a_outer.width;
		const auto outerBottom =
			static_cast<std::uint64_t>(a_outer.baseY) + a_outer.height;
		const auto innerRight =
			static_cast<std::uint64_t>(a_inner.baseX) + a_inner.width;
		const auto innerBottom =
			static_cast<std::uint64_t>(a_inner.baseY) + a_inner.height;
		return a_outer.IsValid() && a_inner.IsValid() &&
		       a_outer.baseX <= a_inner.baseX &&
		       a_outer.baseY <= a_inner.baseY &&
		       innerRight <= outerRight && innerBottom <= outerBottom;
	}

	/** Adds coarse headroom so ordinary head/actor motion does not churn Feature 18 state. */
	[[nodiscard]] inline ComputeSubrect BuildCharacterProviderComputeSubrect(
		const ComputeSubrect& a_required,
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		if (!a_required.Fits(a_width, a_height))
			return {};
		const auto headroomX = ResolveCharacterProviderRoiHeadroom(a_required.width);
		const auto headroomY = ResolveCharacterProviderRoiHeadroom(a_required.height);

		const auto alignDown = [](std::uint32_t a_value) {
			return (a_value / kCharacterProviderRoiAlignment) *
			       kCharacterProviderRoiAlignment;
		};
		const auto guardedEnd = [](
									std::uint32_t a_base,
									std::uint32_t a_extent,
									std::uint32_t a_limit,
									std::uint32_t a_headroom) {
			const auto guarded =
				static_cast<std::uint64_t>(a_base) + a_extent +
				a_headroom;
			const auto aligned =
				(guarded + kCharacterProviderRoiAlignment - 1u) /
				kCharacterProviderRoiAlignment * kCharacterProviderRoiAlignment;
			return static_cast<std::uint32_t>(
				std::min<std::uint64_t>(aligned, a_limit));
		};

		const auto left = alignDown(
			a_required.baseX > headroomX ?
				a_required.baseX - headroomX :
				0u);
		const auto top = alignDown(
			a_required.baseY > headroomY ?
				a_required.baseY - headroomY :
				0u);
		const auto right = guardedEnd(
			a_required.baseX, a_required.width, a_width, headroomX);
		const auto bottom = guardedEnd(
			a_required.baseY, a_required.height, a_height, headroomY);
		return {
			.baseX = left,
			.baseY = top,
			.width = right > left ? right - left : 0u,
			.height = bottom > top ? bottom - top : 0u,
		};
	}

	/**
	 * Keeps contained motion anchored, but retains only a bounded recent-motion
	 * envelope when reclaiming stale work. Contraction needs a 25% area saving and
	 * a cooldown, not identical pixel bounds: ordinary motion cannot indefinitely
	 * retain a full-eye excursion. Growth never postpones that cooldown. Every
	 * changed rectangle still changes Feature 18 history identity at the caller.
	 * Call once per newly prepared source frame; empty input starts a fresh epoch.
	 */
	[[nodiscard]] inline ComputeSubrect ResolveStableCharacterComputeSubrect(
		const ComputeSubrect& a_required,
		std::uint32_t a_width,
		std::uint32_t a_height,
		StableCharacterComputeSubrect& a_state) noexcept
	{
		if (!a_required.Fits(a_width, a_height)) {
			a_state = {};
			return {};
		}
		if (a_state.width != a_width || a_state.height != a_height) {
			a_state = {};
			a_state.width = a_width;
			a_state.height = a_height;
		}
		a_state.recentRequired[a_state.recentCursor] = a_required;
		a_state.recentCursor =
			(a_state.recentCursor + 1u) % kCharacterProviderRoiHistoryFrames;
		a_state.recentCount = std::min(
			a_state.recentCount + 1u, kCharacterProviderRoiHistoryFrames);

		const auto candidate = BuildCharacterProviderComputeSubrect(
			a_required, a_width, a_height);
		if (!candidate.IsValid()) {
			a_state = {};
			return {};
		}
		if (!a_state.provider.Fits(a_width, a_height)) {
			a_state.provider = candidate;
			a_state.framesSinceContraction = 0;
			return a_state.provider;
		}
		if (!ContainsComputeSubrect(a_state.provider, a_required)) {
			a_state.provider = UnionCharacterComputeSubrect(
				a_state.provider, candidate);
		}

		a_state.framesSinceContraction = std::min(
			a_state.framesSinceContraction + 1u,
			kCharacterProviderRoiShrinkDelayFrames);
		if (a_state.framesSinceContraction >= kCharacterProviderRoiShrinkDelayFrames) {
			ComputeSubrect recentBounds{};
			for (std::uint32_t index = 0; index < a_state.recentCount; ++index) {
				recentBounds = UnionCharacterComputeSubrect(
					recentBounds, a_state.recentRequired[index]);
			}
			const auto recentCandidate = BuildCharacterProviderComputeSubrect(
				recentBounds, a_width, a_height);
			const auto providerArea = a_state.provider.Area();
			const auto maximumContractedArea =
				(providerArea / 4u) * 3u + (providerArea % 4u) * 3u / 4u;
			if (recentCandidate.Fits(a_width, a_height) &&
				recentCandidate.Area() <= maximumContractedArea) {
				// The new envelope covers every recent requirement, including this
				// frame. It may recenter as well as shrink; the caller resets history.
				a_state.provider = recentCandidate;
				a_state.framesSinceContraction = 0;
			}
		}
		return a_state.provider;
	}

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
