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
	inline constexpr std::uint32_t kCharacterProviderRoiAlignment = 64;
	inline constexpr std::uint32_t kCharacterProviderRoiHeadroomPixels = 32;
	inline constexpr std::uint32_t kCharacterProviderRoiShrinkDelayFrames = 30;

	struct StableCharacterComputeSubrect
	{
		ComputeSubrect provider{};
		ComputeSubrect pendingShrink{};
		std::uint32_t shrinkCandidateFrames = 0;
	};

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

		const auto alignDown = [](std::uint32_t a_value) {
			return (a_value / kCharacterProviderRoiAlignment) *
			       kCharacterProviderRoiAlignment;
		};
		const auto guardedEnd = [](
			std::uint32_t a_base,
			std::uint32_t a_extent,
			std::uint32_t a_limit) {
			const auto guarded =
				static_cast<std::uint64_t>(a_base) + a_extent +
				kCharacterProviderRoiHeadroomPixels;
			const auto aligned =
				(guarded + kCharacterProviderRoiAlignment - 1u) /
				kCharacterProviderRoiAlignment * kCharacterProviderRoiAlignment;
			return static_cast<std::uint32_t>(
				std::min<std::uint64_t>(aligned, a_limit));
		};

		const auto left = alignDown(
			a_required.baseX > kCharacterProviderRoiHeadroomPixels ?
				a_required.baseX - kCharacterProviderRoiHeadroomPixels :
				0u);
		const auto top = alignDown(
			a_required.baseY > kCharacterProviderRoiHeadroomPixels ?
				a_required.baseY - kCharacterProviderRoiHeadroomPixels :
				0u);
		const auto right = guardedEnd(
			a_required.baseX, a_required.width, a_width);
		const auto bottom = guardedEnd(
			a_required.baseY, a_required.height, a_height);
		return {
			.baseX = left,
			.baseY = top,
			.width = right > left ? right - left : 0u,
			.height = bottom > top ? bottom - top : 0u,
		};
	}

	/**
	 * Keeps the provider ROI stable while the exact character mask moves inside it.
	 * Growth/recentering happens immediately when required pixels escape. Contraction
	 * is delayed and must save at least 25% of provider pixels.
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

		const auto candidate = BuildCharacterProviderComputeSubrect(
			a_required, a_width, a_height);
		if (!candidate.IsValid()) {
			a_state.pendingShrink = {};
			a_state.shrinkCandidateFrames = 0;
			return {};
		}
		if (!a_state.provider.Fits(a_width, a_height) ||
			!ContainsComputeSubrect(a_state.provider, a_required)) {
			a_state.provider = candidate;
			a_state.pendingShrink = {};
			a_state.shrinkCandidateFrames = 0;
			return a_state.provider;
		}

		const auto candidateArea = candidate.Area();
		const auto providerArea = a_state.provider.Area();
		const auto maximumContractedArea =
			(providerArea / 4u) * 3u + (providerArea % 4u) * 3u / 4u;
		const bool meaningfulContraction =
			candidate != a_state.provider &&
			ContainsComputeSubrect(a_state.provider, candidate) &&
			candidateArea <= maximumContractedArea;
		if (!meaningfulContraction) {
			a_state.pendingShrink = {};
			a_state.shrinkCandidateFrames = 0;
			return a_state.provider;
		}

		if (a_state.pendingShrink != candidate) {
			a_state.pendingShrink = candidate;
			a_state.shrinkCandidateFrames = 1;
		} else {
			a_state.shrinkCandidateFrames = std::min(
				a_state.shrinkCandidateFrames + 1u,
				kCharacterProviderRoiShrinkDelayFrames);
		}
		if (a_state.shrinkCandidateFrames >=
			kCharacterProviderRoiShrinkDelayFrames) {
			a_state.provider = candidate;
			a_state.pendingShrink = {};
			a_state.shrinkCandidateFrames = 0;
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
